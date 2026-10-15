/**
 * @file pbrt_flatten_tests.cpp
 * @brief pbrt_flatten tests: Triangles, spheres, disks and cylinders, curves, PLY meshes, malformed geometry, cones and paraboloids, the accelerator, object motion blur of patches and curves, bilinear meshes.
 * The helpers are in pbrt_flatten_test_helpers.h; this file is one part of what used to be a single pbrt_flatten_tests.cpp.
 */

#include "pbrt_flatten_test_helpers.h"

// ===========================================================================
// Triangles
// ===========================================================================

TEST(FlattenTest, UntransformedMeshKeepsItsCoordinates) {
	const FlatScene s = flattenSource(kQuadMesh);
	ASSERT_EQ(s.triangles.size(), 2u);
	EXPECT_DOUBLE_EQ(s.triangles[0].v[0], 0.0);
	EXPECT_DOUBLE_EQ(s.triangles[0].v[3], 1.0);   // second vertex x
	EXPECT_DOUBLE_EQ(s.triangles[0].v[7], 1.0);   // third vertex y
}

TEST(FlattenTest, TrianglemeshWithNoUVLeavesHasUVsFalse) {
	// pbrt-v4's own real "no uv given" default is a fixed (0,0)/(1,0)/(1,1)
	// per triangle CORNER, not shared across faces sharing a vertex -
	// deliberately not synthesized (see pbrt_flatten::Triangle::uv's own
	// comment: it would silently inflate vertex-dedup counts at any shared
	// vertex, a real cost paid by scenes that never read UV at all). Both
	// builders' own barycentric fallback covers the "solid black on GPU"
	// bug this loader used to have instead.
	const FlatScene s = flattenSource(kQuadMesh);
	ASSERT_EQ(s.triangles.size(), 2u);
	EXPECT_FALSE(s.triangles[0].hasUVs);
	EXPECT_FALSE(s.triangles[1].hasUVs);
}

TEST(FlattenTest, TrianglemeshWithRealUVIsThreadedThrough) {
	const FlatScene s = flattenSource(
		"Shape \"trianglemesh\" \"integer indices\" [ 0 1 2  0 2 3 ]\n"
		"  \"point3 P\" [ 0 0 0  1 0 0  1 1 0  0 1 0 ]\n"
		"  \"point2 uv\" [ 0.1 0.2   0.3 0.4   0.5 0.6   0.7 0.8 ]\n");
	ASSERT_EQ(s.triangles.size(), 2u);
	EXPECT_TRUE(s.triangles[0].hasUVs);
	// Face 0 = indices (0,1,2) -> uv pairs 0,1,2.
	EXPECT_DOUBLE_EQ(s.triangles[0].uv[0], 0.1); EXPECT_DOUBLE_EQ(s.triangles[0].uv[1], 0.2);
	EXPECT_DOUBLE_EQ(s.triangles[0].uv[2], 0.3); EXPECT_DOUBLE_EQ(s.triangles[0].uv[3], 0.4);
	EXPECT_DOUBLE_EQ(s.triangles[0].uv[4], 0.5); EXPECT_DOUBLE_EQ(s.triangles[0].uv[5], 0.6);
	// Face 1 = indices (0,2,3) -> uv pairs 0,2,3.
	EXPECT_DOUBLE_EQ(s.triangles[1].uv[0], 0.1); EXPECT_DOUBLE_EQ(s.triangles[1].uv[1], 0.2);
	EXPECT_DOUBLE_EQ(s.triangles[1].uv[2], 0.5); EXPECT_DOUBLE_EQ(s.triangles[1].uv[3], 0.6);
	EXPECT_DOUBLE_EQ(s.triangles[1].uv[4], 0.7); EXPECT_DOUBLE_EQ(s.triangles[1].uv[5], 0.8);
}

TEST(FlattenTest, TrianglemeshUVIsNotSpatiallyTransformedByTheCTM) {
	// A UV pair isn't a world-space point - Translate/Scale/Rotate on the
	// shape must never touch it, unlike "point3 P".
	const FlatScene s = flattenSource(
		"Translate 10 20 30\nScale 2 2 2\n"
		"Shape \"trianglemesh\" \"integer indices\" [ 0 1 2 ]\n"
		"  \"point3 P\" [ 0 0 0  1 0 0  0 1 0 ]\n"
		"  \"point2 uv\" [ 0.25 0.75   0.1 0.2   0.9 0.4 ]\n");
	ASSERT_EQ(s.triangles.size(), 1u);
	EXPECT_DOUBLE_EQ(s.triangles[0].uv[0], 0.25);
	EXPECT_DOUBLE_EQ(s.triangles[0].uv[1], 0.75);
}

TEST(FlattenTest, TrianglemeshWithTooFewUVPairsWarnsAndIgnoresUV) {
	const FlatScene s = flattenSource(
		"Shape \"trianglemesh\" \"integer indices\" [ 0 1 2  0 2 3 ]\n"
		"  \"point3 P\" [ 0 0 0  1 0 0  1 1 0  0 1 0 ]\n"
		"  \"point2 uv\" [ 0.1 0.2   0.3 0.4 ]\n");   // only 2 pairs, needs 4
	ASSERT_EQ(s.triangles.size(), 2u);
	EXPECT_TRUE(warnedAbout(s, "uv"));
	// Same "refused wholesale" treatment as too-few normals - hasUVs stays
	// false, same as no "uv" param at all.
	EXPECT_FALSE(s.triangles[0].hasUVs);
}

TEST(FlattenTest, LoopsubdivDoesNotThreadUV) {
	// UV parsing is scoped to trianglemesh only - loopsubdiv doesn't thread
	// it through this loader at all yet (a separate, smaller, documented
	// gap); its own "point3 P"/"integer indices" parsing is entirely
	// separate code from trianglemesh's, so this just confirms hasUVs stays
	// at its default false rather than somehow picking up stale state.
	const FlatScene s = flattenSource(
		"Shape \"loopsubdiv\" \"integer indices\" [ 0 1 2  0 2 3 ]\n"
		"  \"point3 P\" [ 0 0 0  1 0 0  1 1 0  0 1 0 ] \"integer levels\" [ 0 ]\n");
	ASSERT_EQ(s.triangles.size(), 2u);
	EXPECT_FALSE(s.triangles[0].hasUVs);
}

TEST(FlattenTest, ReverseOrientationNegatesAuthoredNormals) {
	// pbrt-v4's TriangleMesh negates every vertex N under ReverseOrientation (util/mesh.cpp:49-56);
	// the authored normal decides which side faces out and, for an emitter, which side emits.
	const std::string mesh = R"(Shape "trianglemesh" "integer indices" [ 0 1 2 ]
  "point3 P" [ 0 0 0  1 0 0  0 1 0 ]
  "normal N" [ 0 0 1  0 0 1  0 0 1 ]
)";
	const FlatScene plain = flattenSource(mesh);
	const FlatScene reversed = flattenSource("ReverseOrientation\n" + mesh);
	ASSERT_EQ(plain.triangles.size(), 1u);
	ASSERT_EQ(reversed.triangles.size(), 1u);
	ASSERT_TRUE(plain.triangles[0].hasNormals);
	ASSERT_TRUE(reversed.triangles[0].hasNormals);
	for (int v = 0; v < 3; ++v) {
		EXPECT_DOUBLE_EQ(plain.triangles[0].n[v * 3 + 2], 1.0);
		EXPECT_DOUBLE_EQ(reversed.triangles[0].n[v * 3 + 2], -1.0);
	}
}

TEST(FlattenTest, DegenerateTrianglesAreDroppedWithAWarning) {
	// Face 0 repeats a vertex, face 1 is collinear (zero area), face 2 is real.
	const FlatScene s = flattenSource(R"(Shape "trianglemesh" "integer indices" [ 0 1 1  0 1 3  0 1 2 ]
  "point3 P" [ 0 0 0  1 0 0  0 1 0  2 0 0 ]
)");
	EXPECT_EQ(s.triangles.size(), 1u);
	EXPECT_TRUE(warnedAbout(s, "degenerate"));
}

TEST(FlattenTest, TranslationIsBakedIntoEveryVertex) {
	const FlatScene s = flattenSource(std::string("Translate 10 20 30\n") + kQuadMesh);
	ASSERT_EQ(s.triangles.size(), 2u);
	EXPECT_DOUBLE_EQ(s.triangles[0].v[0], 10.0);
	EXPECT_DOUBLE_EQ(s.triangles[0].v[1], 20.0);
	EXPECT_DOUBLE_EQ(s.triangles[0].v[2], 30.0);
	// the second triangle shares vertex 0 and must land in the same place
	EXPECT_DOUBLE_EQ(s.triangles[1].v[0], 10.0);
	EXPECT_DOUBLE_EQ(s.triangles[1].v[1], 20.0);
}

TEST(FlattenTest, ScaleAndRotationCompose) {
	// Scale by 2 then rotate 90 degrees about Z: (1,0,0) -> (2,0,0) -> (0,2,0).
	const FlatScene s = flattenSource(
		"Rotate 90 0 0 1\n"
		"Scale 2 2 2\n" + std::string(kQuadMesh));
	ASSERT_EQ(s.triangles.size(), 2u);
	EXPECT_NEAR(s.triangles[0].v[3], 0.0, 1e-9);
	EXPECT_NEAR(s.triangles[0].v[4], 2.0, 1e-9);
}

TEST(FlattenTest, TransformsRespectAttributeScope) {
	const FlatScene s = flattenSource(
		std::string("AttributeBegin\n  Translate 100 0 0\n") + kQuadMesh + "AttributeEnd\n" + kQuadMesh);
	ASSERT_EQ(s.triangles.size(), 4u);
	EXPECT_DOUBLE_EQ(s.triangles[0].v[0], 100.0) << "inside the scope";
	EXPECT_DOUBLE_EQ(s.triangles[2].v[0], 0.0)   << "outside it";
}

TEST(FlattenTest, MaterialAndAreaLightIndicesSurvive) {
	const FlatScene s = flattenSource(
		std::string("Material \"conductor\"\n"
					"AttributeBegin\n"
					"  AreaLightSource \"diffuse\" \"rgb L\" [ 5 5 5 ]\n")
		+ kQuadMesh + "AttributeEnd\n" + kQuadMesh);
	ASSERT_EQ(s.triangles.size(), 4u);
	EXPECT_EQ(s.triangles[0].areaLight, 0) << "emissive inside the scope";
	EXPECT_EQ(s.triangles[3].areaLight, -1) << "not emissive outside it";
	EXPECT_EQ(s.triangles[0].material, s.triangles[3].material);
	EXPECT_GE(s.triangles[0].material, 0);
}

TEST(FlattenTest, AreaLightFilenameAndTwoSidedAreParsed) {
	const FlatScene s = flattenSource(
		std::string("AttributeBegin\n"
					"  AreaLightSource \"diffuse\" \"string filename\" [ \"glow.png\" ]"
					" \"bool twosided\" [ true ]\n")
		+ kQuadMesh + "AttributeEnd\n");
	ASSERT_EQ(s.triangles[0].areaLight, 0);
	ASSERT_EQ(s.areaLights.size(), 1u);
	EXPECT_EQ(s.areaLights[0].filename, "glow.png");
	EXPECT_TRUE(s.areaLights[0].twoSided);
}

TEST(FlattenTest, AreaLightFilenameAndTwoSidedDefaultToUnsetAndFalse) {
	const FlatScene s = flattenSource(
		std::string("AttributeBegin\n  AreaLightSource \"diffuse\" \"rgb L\" [ 5 5 5 ]\n")
		+ kQuadMesh + "AttributeEnd\n");
	ASSERT_EQ(s.areaLights.size(), 1u);
	EXPECT_TRUE(s.areaLights[0].filename.empty());
	EXPECT_FALSE(s.areaLights[0].twoSided);
}

// "float power" (total emitted radiometric power Phi, watts) - an
// alternative to specifying L directly. Each test below picks a power value
// engineered to reproduce the same DEFAULT scale=1.0 an equivalent
// power-free light already gets (see each formula's own comment) - a light
// whose "power" input round-trips back to no scale change is a strong,
// exactly-checkable correctness signal, not just "some scale got applied".
TEST(FlattenTest, AreaLightPowerConvertsToScaleForUnitQuad) {
	// pbrt-v4 applies "power" to each triangle of a mesh on its own: kQuadMesh is
	// a unit square of two triangles of area 0.5, so Phi = L*pi*0.5 = pi/2 per
	// triangle, and power=pi/2 should reproduce scale=1.0 exactly.
	const FlatScene s = flattenSource(
		std::string("AttributeBegin\n  AreaLightSource \"diffuse\" \"float power\" [ 1.57079632679489661923 ]\n")
		+ kQuadMesh + "AttributeEnd\n");
	ASSERT_EQ(s.areaLights.size(), 1u);
	EXPECT_TRUE(s.areaLights[0].hasPower);
	EXPECT_NEAR(s.areaLights[0].scale, 1.0, 1e-9);
}

TEST(FlattenTest, AreaLightPowerFollowsEachPrimitivesOwnArea) {
	// A unit quad (two triangles of 0.5) and a quad of side 2 (two of 2.0) under
	// one AreaLightSource: pbrt-v4 gives every triangle the scale its OWN area
	// asks for (power=pi: 2 for a small triangle, 0.5 for a big one), not one
	// scale from the summed area. The two small triangles share a light, the two
	// big ones another.
	const FlatScene s = flattenSource(
		std::string("AttributeBegin\n  AreaLightSource \"diffuse\" \"float power\" [ 3.14159265358979323846 ]\n")
		+ kQuadMesh +
		"Shape \"trianglemesh\" \"integer indices\" [ 0 1 2  0 2 3 ]\n"
		"  \"point3 P\" [ 2 0 0  4 0 0  4 2 0  2 2 0 ]\n"
		"AttributeEnd\n");
	ASSERT_EQ(s.areaLights.size(), 2u);
	ASSERT_EQ(s.triangles.size(), 4u);
	EXPECT_EQ(s.triangles[0].areaLight, s.triangles[1].areaLight);
	EXPECT_EQ(s.triangles[2].areaLight, s.triangles[3].areaLight);
	EXPECT_NE(s.triangles[0].areaLight, s.triangles[2].areaLight);
	EXPECT_NEAR(s.areaLights[s.triangles[0].areaLight].scale, 2.0, 1e-9);
	EXPECT_NEAR(s.areaLights[s.triangles[2].areaLight].scale, 0.5, 1e-9);
}

TEST(FlattenTest, AreaLightPowerHonoursTwoSidedAndExistingLColour) {
	// twoSided doubles the area term (kE = pi*area*2). pbrt-v4 does not divide by
	// the colour's luminance (SpectrumToPhotometric of an RGB light uses only its
	// illuminant), so an L of (2,0,0) keeps its 2 and only `scale` changes.
	const FlatScene s = flattenSource(
		std::string("AttributeBegin\n  AreaLightSource \"diffuse\" \"rgb L\" [ 2 0 0 ]"
					" \"bool twosided\" [ true ] \"float power\" [ 3.14159265358979323846 ]\n")
		+ kQuadMesh + "AttributeEnd\n");
	ASSERT_EQ(s.areaLights.size(), 1u);
	// Per triangle: kE = pi*area(0.5)*2 = pi; scale = power/kE = 1.
	EXPECT_NEAR(s.areaLights[0].scale, 1.0, 1e-9);
	EXPECT_DOUBLE_EQ(s.areaLights[0].L[0], 2.0);
	EXPECT_DOUBLE_EQ(s.areaLights[0].L[1], 0.0);
	EXPECT_DOUBLE_EQ(s.areaLights[0].L[2], 0.0);
}

TEST(FlattenTest, AreaLightPowerOnDiskWarnsAndLeavesScaleUnchanged) {
	// Disk/Cylinder area (unlike Sphere) are never baked to world space -
	// see Disk/Cylinder's own struct comment - so this loader deliberately
	// doesn't attempt a world-space area for them; "power" on one should
	// warn and fall back to scale=1.0 (as given) rather than silently
	// mis-stating the light's total output.
	const FlatScene s = flattenSource(
		"AttributeBegin\n"
		"  AreaLightSource \"diffuse\" \"float power\" [ 10 ]\n"
		"  Shape \"disk\" \"float radius\" [ 1 ]\n"
		"AttributeEnd\n");
	ASSERT_EQ(s.areaLights.size(), 1u);
	EXPECT_DOUBLE_EQ(s.areaLights[0].scale, 1.0);
	EXPECT_TRUE(warnedAbout(s, "area light \"power\""));
}

TEST(FlattenTest, PointLightPowerConvertsToIntensityScale) {
	// I = Phi/(4*pi); with the default I=(1,1,1) (luminance 1.0),
	// power=4*pi should reproduce scale=1.0 exactly - the same value a
	// power-free point light already defaults to.
	const FlatScene s = flattenSource(
		"LightSource \"point\" \"float power\" [ 12.566370614359172 ]\n");
	ASSERT_EQ(s.punctualLights.size(), 1u);
	EXPECT_NEAR(s.punctualLights[0].scale, 1.0, 1e-9);
}

TEST(FlattenTest, SpotLightPowerConvertsToIntensityScale) {
	// Default coneangle=30/conedeltaangle=5 (falloffStart=25) closed-form
	// kE = 2*pi*((1-cos25)+(cos25-cos30)/2) = 0.7152363751826972 - power
	// set to exactly that should reproduce scale=1.0, the pre-existing
	// power-free default.
	const FlatScene s = flattenSource(
		"LightSource \"spot\" \"float power\" [ 0.7152363751826972 ]\n");
	ASSERT_EQ(s.punctualLights.size(), 1u);
	EXPECT_NEAR(s.punctualLights[0].scale, 1.0, 1e-8);
}

TEST(FlattenTest, DistantLightPowerWarnsAndIsIgnored) {
	const FlatScene s = flattenSource(
		"LightSource \"distant\" \"float power\" [ 10 ]\n");
	ASSERT_EQ(s.punctualLights.size(), 1u);
	EXPECT_DOUBLE_EQ(s.punctualLights[0].scale, 1.0);
	EXPECT_TRUE(warnedAbout(s, "distant light \"power\""));
}

TEST(FlattenTest, GoniometricLightPowerWarnsAndIsIgnored) {
	const FlatScene s = flattenSource(
		"LightSource \"goniometric\" \"float power\" [ 10 ]\n");
	ASSERT_EQ(s.punctualLights.size(), 1u);
	EXPECT_DOUBLE_EQ(s.punctualLights[0].scale, 1.0);
	EXPECT_TRUE(warnedAbout(s, "goniometric light \"power\""));
}

TEST(FlattenTest, ProjectionLightPowerWarnsAndIsIgnored) {
	const FlatScene s = flattenSource(
		"LightSource \"projection\" \"string filename\" [ \"slide.png\" ] \"float power\" [ 10 ]\n");
	ASSERT_EQ(s.punctualLights.size(), 1u);
	EXPECT_DOUBLE_EQ(s.punctualLights[0].scale, 1.0);
	EXPECT_TRUE(warnedAbout(s, "projection light \"power\""));
}

// Review-fix regression tests: a zero or negative "power" must be treated as
// "not given" (matching pbrt-v4's own `if (phi_v > 0)` guards), not applied
// literally - a bare multiply would otherwise zero out or even negate the
// light's scale.
TEST(FlattenTest, PointLightNegativePowerIsIgnored) {
	const FlatScene s = flattenSource(
		"LightSource \"point\" \"float power\" [ -10 ]\n");
	ASSERT_EQ(s.punctualLights.size(), 1u);
	EXPECT_DOUBLE_EQ(s.punctualLights[0].scale, 1.0);
}

TEST(FlattenTest, PointLightZeroPowerIsIgnored) {
	const FlatScene s = flattenSource(
		"LightSource \"point\" \"float power\" [ 0 ]\n");
	ASSERT_EQ(s.punctualLights.size(), 1u);
	EXPECT_DOUBLE_EQ(s.punctualLights[0].scale, 1.0);
}

TEST(FlattenTest, SpotLightNegativePowerIsIgnored) {
	const FlatScene s = flattenSource(
		"LightSource \"spot\" \"float power\" [ -10 ]\n");
	ASSERT_EQ(s.punctualLights.size(), 1u);
	EXPECT_DOUBLE_EQ(s.punctualLights[0].scale, 1.0);
}

TEST(FlattenTest, AreaLightNegativePowerIsIgnored) {
	const FlatScene s = flattenSource(
		std::string("AttributeBegin\n  AreaLightSource \"diffuse\" \"float power\" [ -10 ]\n")
		+ kQuadMesh + "AttributeEnd\n");
	ASSERT_EQ(s.areaLights.size(), 1u);
	EXPECT_DOUBLE_EQ(s.areaLights[0].scale, 1.0);
}

TEST(FlattenTest, SpotLightDegenerateConeWarnsAndPowerIsIgnored) {
	// coneangle=conedeltaangle=0 makes cosFalloffStart==cosFalloffEnd==1, so
	// kE (the solid-angle-like denominator) is exactly 0 - no angle to spread
	// Phi over. Should warn (matching the area-light zero-area case) rather
	// than silently drop the requested power with no diagnostic.
	const FlatScene s = flattenSource(
		"LightSource \"spot\" \"float coneangle\" [ 0 ] \"float conedeltaangle\" [ 0 ] "
		"\"float power\" [ 50 ]\n");
	ASSERT_EQ(s.punctualLights.size(), 1u);
	EXPECT_DOUBLE_EQ(s.punctualLights[0].scale, 1.0);
	EXPECT_TRUE(warnedAbout(s, "spot light \"power\""));
}

TEST(FlattenTest, AreaLightPowerWithFilenameWarnsAndIsIgnored) {
	// "filename" wins over L entirely for what's actually rendered - the
	// power formula is derived from L, which has no relationship to the
	// image's real average radiance. Should warn instead of silently
	// computing a scale against a fictitious flat colour.
	const FlatScene s = flattenSource(
		std::string("AttributeBegin\n  AreaLightSource \"diffuse\" \"string filename\" [ \"glow.png\" ]"
					" \"float power\" [ 10 ]\n")
		+ kQuadMesh + "AttributeEnd\n");
	ASSERT_EQ(s.areaLights.size(), 1u);
	EXPECT_DOUBLE_EQ(s.areaLights[0].scale, 1.0);
	EXPECT_TRUE(warnedAbout(s, "area light \"power\""));
	EXPECT_TRUE(warnedAbout(s, "\"filename\""));
}

TEST(FlattenTest, AreaLightPowerOnMixedShapeKindsWarnsAndDiscardsComputedArea) {
	// One AreaLightSource shared by a measurable trianglemesh AND an
	// unsupported disk: the whole light's power resolution should still bail
	// out (a partial area would understate the true total and overshoot the
	// target power), but the warning should distinguish "mixed with a
	// measurable shape" from "attached only to an unsupported shape kind".
	const FlatScene s = flattenSource(
		std::string("AttributeBegin\n  AreaLightSource \"diffuse\" \"float power\" [ 10 ]\n")
		+ kQuadMesh +
		"  Shape \"disk\" \"float radius\" [ 1 ]\n"
		"AttributeEnd\n");
	ASSERT_EQ(s.areaLights.size(), 1u);
	EXPECT_DOUBLE_EQ(s.areaLights[0].scale, 1.0);
	EXPECT_TRUE(warnedAbout(s, "mix of a measurable shape"));
}

// ===========================================================================
// Spheres
// ===========================================================================

TEST(FlattenTest, SphereCentreIsTransformedAndRadiusScaled) {
	const FlatScene s = flattenSource(
		"Translate 1 2 3\n"
		"Scale 4 4 4\n"
		"Shape \"sphere\" \"float radius\" [ 0.5 ]\n");
	ASSERT_EQ(s.spheres.size(), 1u);
	EXPECT_DOUBLE_EQ(s.spheres[0].center[0], 1.0);
	EXPECT_DOUBLE_EQ(s.spheres[0].center[2], 3.0);
	EXPECT_DOUBLE_EQ(s.spheres[0].radius, 2.0) << "0.5 scaled by 4";
}

TEST(FlattenTest, UniformScaleDoesNotWarn) {
	const FlatScene s = flattenSource(
		"Scale 3 3 3\nShape \"sphere\" \"float radius\" [ 1 ]\n");
	EXPECT_FALSE(warnedAbout(s, "ellipsoid"));
	EXPECT_DOUBLE_EQ(s.spheres[0].radius, 3.0);
}

TEST(FlattenTest, NonUniformScaleOnASphereIsReportedNotSilentlyRounded) {
	// A squashed sphere cannot be represented, and quietly emitting a round one
	// is exactly the difference nobody notices against a reference image.
	const FlatScene s = flattenSource(
		"Scale 1 5 1\nShape \"sphere\" \"float radius\" [ 1 ]\n");
	ASSERT_EQ(s.spheres.size(), 1u);
	EXPECT_TRUE(warnedAbout(s, "ellipsoid"));
	EXPECT_DOUBLE_EQ(s.spheres[0].radius, 5.0) << "largest axis is used";
}

TEST(FlattenTest, SphereWithNoClippingParamsIsNotMarkedClipped) {
	// Every existing scene must leave Sphere::clipped false - the CPU builder
	// keys off exactly this to keep using the plain baked center/radius path.
	const FlatScene s = flattenSource("Shape \"sphere\" \"float radius\" [ 1 ]\n");
	ASSERT_EQ(s.spheres.size(), 1u);
	EXPECT_FALSE(s.spheres[0].clipped);
}

TEST(FlattenTest, SphereZMinZMaxMarksItClippedAndCarriesObjectSpaceValues) {
	const FlatScene s = flattenSource(
		"Translate 1 2 3\n"
		"Shape \"sphere\" \"float radius\" [ 2 ] \"float zmin\" [ -1 ] \"float zmax\" [ 1 ]\n");
	ASSERT_EQ(s.spheres.size(), 1u);
	EXPECT_TRUE(s.spheres[0].clipped);
	EXPECT_DOUBLE_EQ(s.spheres[0].radiusLocal, 2.0)
		<< "object-space, matching the declared radius directly - not scaled";
	EXPECT_DOUBLE_EQ(s.spheres[0].zMin, -1.0);
	EXPECT_DOUBLE_EQ(s.spheres[0].zMax, 1.0);
	// The object-to-world transform (needed once clipping breaks rotation
	// invariance) must carry the real translation, not identity.
	EXPECT_DOUBLE_EQ(s.spheres[0].xform[3], 1.0);
	EXPECT_DOUBLE_EQ(s.spheres[0].xform[11], 3.0);
}

TEST(FlattenTest, SphereZMinZMaxAreReorderedWhenAuthoredSwapped) {
	// Real pbrt-v4 tolerates zmin/zmax given in either order (Sphere::Create
	// does Clamp(min(zmin,zmax),-r,r)/Clamp(max(zmin,zmax),-r,r)) - a scene
	// (or a converter) that writes them swapped must still produce the same
	// cap, not an empty [zMin,zMax] range that clips away the entire sphere.
	const FlatScene s = flattenSource(
		"Shape \"sphere\" \"float radius\" [ 1 ] \"float zmin\" [ 1 ] \"float zmax\" [ -1 ]\n");
	ASSERT_EQ(s.spheres.size(), 1u);
	EXPECT_FALSE(s.spheres[0].clipped)
		<< "reordered to (-1,1), which exactly matches the full-sphere default";
	EXPECT_DOUBLE_EQ(s.spheres[0].zMin, -1.0);
	EXPECT_DOUBLE_EQ(s.spheres[0].zMax, 1.0);
}

TEST(FlattenTest, SphereZMinZMaxAreClampedToTheRadius) {
	// A scene that writes an out-of-range zmax (e.g. authored against the
	// wrong radius after an edit) must not produce a [zMin,zMax] wider than
	// the sphere itself - real pbrt-v4 clamps to [-r,r].
	const FlatScene s = flattenSource(
		"Shape \"sphere\" \"float radius\" [ 1 ] \"float zmax\" [ 5 ]\n");
	ASSERT_EQ(s.spheres.size(), 1u);
	EXPECT_FALSE(s.spheres[0].clipped) << "clamped to 1.0, matching the full-sphere default";
	EXPECT_DOUBLE_EQ(s.spheres[0].zMax, 1.0);
}

TEST(FlattenTest, SpherePhiMaxAloneAlsoMarksItClipped) {
	const FlatScene s = flattenSource(
		"Shape \"sphere\" \"float radius\" [ 1 ] \"float phimax\" [ 90 ]\n");
	ASSERT_EQ(s.spheres.size(), 1u);
	EXPECT_TRUE(s.spheres[0].clipped);
	EXPECT_DOUBLE_EQ(s.spheres[0].phiMaxDeg, 90.0);
	EXPECT_DOUBLE_EQ(s.spheres[0].zMin, -1.0) << "zmin/zmax keep their pbrt defaults";
	EXPECT_DOUBLE_EQ(s.spheres[0].zMax, 1.0);
}

TEST(FlattenTest, SphereZMinZMaxMatchingTheFullSphereIsNotMarkedClipped) {
	// A scene that spells out the pbrt defaults explicitly (zmin=-radius,
	// zmax=radius, phimax=360) is still a full sphere - the equality check
	// must treat this the same as omitting the params entirely.
	const FlatScene s = flattenSource(
		"Shape \"sphere\" \"float radius\" [ 3 ] \"float zmin\" [ -3 ] "
		"\"float zmax\" [ 3 ] \"float phimax\" [ 360 ]\n");
	ASSERT_EQ(s.spheres.size(), 1u);
	EXPECT_FALSE(s.spheres[0].clipped);
}

TEST(FlattenTest, ClippedSphereWithMediumInterfaceWarnsButKeepsTheMediumFieldForGpu) {
	// A clipped sphere is an open shell (a hole where the clip cut it away),
	// which CPU's constant_medium can't bound (requires a watertight
	// boundary - two ordered hits per ray) - see sphere_clipped_hittable.h's
	// own comment. But GPU always renders every sphere (clipped or not) as
	// the same full, closed, baked-radius shape, which HAS no such problem
	// - so `medium` itself must stay the real resolved index (for GPU's own
	// independent read of it); only the new, CPU-only
	// `cpuMediumUnsupported` flag records the CPU-specific limitation,
	// consulted by pbrt_cpu_builder.h alone.
	const FlatScene s = flattenSource(
		"MakeNamedMedium \"fog\" \"string type\" \"homogeneous\"\n"
		"  \"rgb sigma_a\" [ 0.1 0.2 0.3 ] \"rgb sigma_s\" [ 1 2 3 ]\n"
		"AttributeBegin\n"
		"  MediumInterface \"fog\" \"\"\n"
		"  Shape \"sphere\" \"float radius\" [ 1 ] \"float zmin\" [ 0 ]\n"
		"AttributeEnd\n");
	ASSERT_EQ(s.spheres.size(), 1u);
	EXPECT_TRUE(s.spheres[0].clipped);
	EXPECT_EQ(s.spheres[0].medium, 0) << "GPU still needs the real index";
	EXPECT_TRUE(s.spheres[0].cpuMediumUnsupported);
	EXPECT_TRUE(warnedAbout(s, "clipped sphere"));
}

TEST(FlattenTest, UnclippedSphereWithMediumInterfaceStillWorksNormally) {
	// Regression guard for the fix above: the new clipped-specific gate must
	// not accidentally catch the ordinary, already-working unclipped case.
	const FlatScene s = flattenSource(
		"MakeNamedMedium \"fog\" \"string type\" \"homogeneous\"\n"
		"  \"rgb sigma_a\" [ 0.1 0.2 0.3 ] \"rgb sigma_s\" [ 1 2 3 ]\n"
		"AttributeBegin\n"
		"  MediumInterface \"fog\" \"\"\n"
		"  Shape \"sphere\" \"float radius\" [ 1 ]\n"
		"AttributeEnd\n");
	ASSERT_EQ(s.spheres.size(), 1u);
	EXPECT_FALSE(s.spheres[0].clipped);
	EXPECT_EQ(s.spheres[0].medium, 0);
	EXPECT_FALSE(s.spheres[0].cpuMediumUnsupported);
	EXPECT_FALSE(warnedAbout(s, "clipped sphere"));
}

TEST(FlattenTest, NonUniformScaleOnAClippedSphereNeedsNoWarningOnEitherBackend) {
	// Both CPU (sphere_clipped_hittable.h) and GPU (pbrt_gpu_builder.h's
	// ClippedSphere branch) now carry a clipped sphere's real object<->world
	// transform and handle non-uniform scale exactly - unlike an unclipped
	// sphere, which both backends still bake down to a single world-space
	// center+radius and so still need the "would be an ellipsoid" warning.
	const FlatScene clipped = flattenSource(
		"Scale 1 5 1\nShape \"sphere\" \"float radius\" [ 1 ] \"float zmin\" [ 0 ]\n");
	ASSERT_EQ(clipped.spheres.size(), 1u);
	EXPECT_TRUE(clipped.spheres[0].clipped);
	EXPECT_FALSE(warnedAbout(clipped, "ellipsoid"))
		<< "the ellipsoid/largest-radius wording is false for both backends' exact clipped path";
	EXPECT_FALSE(warnedAbout(clipped, "non-uniform scale"))
		<< "no warning is needed at all once both backends handle it exactly";

	const FlatScene unclipped = flattenSource(
		"Scale 1 5 1\nShape \"sphere\" \"float radius\" [ 1 ]\n");
	EXPECT_TRUE(warnedAbout(unclipped, "ellipsoid"))
		<< "unclipped spheres keep the original wording unchanged";
}

TEST(FlattenTest, ClippedSphereKeepsItsBakedCenterAndRadiusToo) {
	// A clipped sphere's real hit-testing shape lives in radiusLocal/zMin/
	// zMax/phiMax/xform now (both backends honor the clip), but center/
	// radius stay populated too - GPU's ClippedSphere branch (pbrt_gpu_
	// builder.h) reuses them purely so the pre-existing full-sphere-cone NEE
	// machinery has a cone to sample when this sphere is also an
	// AreaLightSource, without any new clip-aware sampling code (the same
	// accepted approximation CPU's own sphere_clipped_hittable.h already
	// documents for this combination) - so those must stay populated and
	// correct even when clipped is true.
	const FlatScene s = flattenSource(
		"Translate 0 0 10\n"
		"Shape \"sphere\" \"float radius\" [ 2 ] \"float zmin\" [ 0 ]\n");
	ASSERT_EQ(s.spheres.size(), 1u);
	EXPECT_TRUE(s.spheres[0].clipped);
	EXPECT_DOUBLE_EQ(s.spheres[0].center[2], 10.0);
	EXPECT_DOUBLE_EQ(s.spheres[0].radius, 2.0);
}

// ===========================================================================
// Disks / Cylinders
// ===========================================================================

TEST(FlattenTest, DiskParamsAreReadWithPbrtDefaults) {
	const FlatScene s = flattenSource("Shape \"disk\"\n");
	ASSERT_EQ(s.disks.size(), 1u);
	EXPECT_DOUBLE_EQ(s.disks[0].radius, 1.0);
	EXPECT_DOUBLE_EQ(s.disks[0].innerRadius, 0.0);
	EXPECT_DOUBLE_EQ(s.disks[0].height, 0.0);
	EXPECT_DOUBLE_EQ(s.disks[0].phiMaxDeg, 360.0);
}

TEST(FlattenTest, DiskParamsAreReadWhenGiven) {
	const FlatScene s = flattenSource(
		"Shape \"disk\" \"float radius\" [ 3 ] \"float innerradius\" [ 1 ] "
		"\"float height\" [ 2 ] \"float phimax\" [ 270 ]\n");
	ASSERT_EQ(s.disks.size(), 1u);
	EXPECT_DOUBLE_EQ(s.disks[0].radius, 3.0);
	EXPECT_DOUBLE_EQ(s.disks[0].innerRadius, 1.0);
	EXPECT_DOUBLE_EQ(s.disks[0].height, 2.0);
	EXPECT_DOUBLE_EQ(s.disks[0].phiMaxDeg, 270.0);
}

TEST(FlattenTest, DiskCarriesItsCTMUnbakedRatherThanBeingApproximated) {
	// Unlike Sphere (rotation-invariant, baked to world-space center+radius -
	// see NonUniformScaleOnASphereIsReportedNotSilentlyRounded above), a disk
	// is not rotation-invariant, so its xform is kept as-is for the CPU/GPU
	// builders to apply exactly at intersection time - see pbrt_flatten::
	// Disk's own comment for why. This just pins that the CTM in effect at
	// the Shape directive is what ends up on the flat struct.
	const FlatScene s = flattenSource("Translate 10 20 30\nShape \"disk\"\n");
	ASSERT_EQ(s.disks.size(), 1u);
	// Row-major affine: translation lives in column 3 of each row (m[3],
	// m[7], m[11] - see pbrt_scene::Matrix4's own layout, matching
	// transform_instance.h's apply_point()).
	EXPECT_DOUBLE_EQ(s.disks[0].xform[3], 10.0);
	EXPECT_DOUBLE_EQ(s.disks[0].xform[7], 20.0);
	EXPECT_DOUBLE_EQ(s.disks[0].xform[11], 30.0);
}

TEST(FlattenTest, CylinderParamsAreReadWithPbrtDefaults) {
	const FlatScene s = flattenSource("Shape \"cylinder\"\n");
	ASSERT_EQ(s.cylinders.size(), 1u);
	EXPECT_DOUBLE_EQ(s.cylinders[0].radius, 1.0);
	EXPECT_DOUBLE_EQ(s.cylinders[0].zMin, -1.0);
	EXPECT_DOUBLE_EQ(s.cylinders[0].zMax, 1.0);
	EXPECT_DOUBLE_EQ(s.cylinders[0].phiMaxDeg, 360.0);
}

TEST(FlattenTest, CylinderParamsAreReadWhenGiven) {
	const FlatScene s = flattenSource(
		"Shape \"cylinder\" \"float radius\" [ 2 ] \"float zmin\" [ -5 ] "
		"\"float zmax\" [ 5 ] \"float phimax\" [ 180 ]\n");
	ASSERT_EQ(s.cylinders.size(), 1u);
	EXPECT_DOUBLE_EQ(s.cylinders[0].radius, 2.0);
	EXPECT_DOUBLE_EQ(s.cylinders[0].zMin, -5.0);
	EXPECT_DOUBLE_EQ(s.cylinders[0].zMax, 5.0);
	EXPECT_DOUBLE_EQ(s.cylinders[0].phiMaxDeg, 180.0);
}

TEST(FlattenTest, DiskAndCylinderRespectMaterialAreaLightAndMedium) {
	const FlatScene s = flattenSource(
		"MakeNamedMedium \"fog\" \"string type\" \"homogeneous\"\n"
		"Material \"diffuse\" \"rgb reflectance\" [ .5 .5 .5 ]\n"
		"AttributeBegin\n"
		"  MediumInterface \"fog\" \"\"\n"
		"  AreaLightSource \"diffuse\" \"rgb L\" [ 5 5 5 ]\n"
		"  Shape \"disk\"\n"
		"  Shape \"cylinder\"\n"
		"AttributeEnd\n");
	ASSERT_EQ(s.disks.size(), 1u);
	ASSERT_EQ(s.cylinders.size(), 1u);
	EXPECT_EQ(s.disks[0].material, 0);
	EXPECT_EQ(s.disks[0].areaLight, 0);
	EXPECT_EQ(s.disks[0].medium, 0);
	EXPECT_EQ(s.cylinders[0].material, 0);
	EXPECT_EQ(s.cylinders[0].areaLight, 0);
	EXPECT_EQ(s.cylinders[0].medium, 0);
}

// ===========================================================================
// Curve
// ===========================================================================

namespace {
const char *kSingleSegmentCurve =
	"Shape \"curve\"\n"
	"  \"point3 P\" [ 0 0 0  1 1 0  2 1 0  3 0 0 ]\n";
} // namespace

TEST(FlattenTest, CurveParamsAreReadWithPbrtDefaults) {
	const FlatScene s = flattenSource(kSingleSegmentCurve);
	ASSERT_EQ(s.curves.size(), 1u);
	EXPECT_EQ(s.curves[0].nSegments, 1);
	EXPECT_DOUBLE_EQ(s.curves[0].width0, 1.0);
	EXPECT_DOUBLE_EQ(s.curves[0].width1, 1.0);
	EXPECT_EQ(s.curves[0].curveType, "flat");
	ASSERT_EQ(s.curves[0].cp.size(), 12u);
	EXPECT_DOUBLE_EQ(s.curves[0].cp[0], 0.0);
	EXPECT_DOUBLE_EQ(s.curves[0].cp[9], 3.0);
}

TEST(FlattenTest, CurveWidthParamsAreReadWhenGiven) {
	const FlatScene s = flattenSource(
		"Shape \"curve\" \"string type\" [ \"cylinder\" ] "
		"\"float width0\" [ 0.2 ] \"float width1\" [ 0.05 ]\n"
		"  \"point3 P\" [ 0 0 0  1 1 0  2 1 0  3 0 0 ]\n");
	ASSERT_EQ(s.curves.size(), 1u);
	EXPECT_DOUBLE_EQ(s.curves[0].width0, 0.2);
	EXPECT_DOUBLE_EQ(s.curves[0].width1, 0.05);
	EXPECT_EQ(s.curves[0].curveType, "cylinder");
}

TEST(FlattenTest, CurveWidthFollowsTheTransformLikeTheControlPoints) {
	// pbrt-v4 intersects a curve in object space: the width is an object-space length, so under Scale 8 the control points and the width both grow 8 times.
	const FlatScene s = flattenSource(
		"AttributeBegin\n  Scale 8 8 8\n  Shape \"curve\" \"float width0\" [ 0.2 ] \"float width1\" [ 0.05 ]\n"
		"    \"point3 P\" [ 0 0 0  1 1 0  2 1 0  3 0 0 ]\nAttributeEnd\n");
	ASSERT_EQ(s.curves.size(), 1u);
	EXPECT_NEAR(s.curves[0].cp[9], 24.0, 1e-9);
	EXPECT_NEAR(s.curves[0].width0, 1.6, 1e-9);
	EXPECT_NEAR(s.curves[0].width1, 0.4, 1e-9);
}

TEST(FlattenTest, CurveWidthShorthandSetsBothEndpoints) {
	const FlatScene s = flattenSource(
		std::string("Shape \"curve\" \"float width\" [ 0.3 ]\n") +
		"  \"point3 P\" [ 0 0 0  1 1 0  2 1 0  3 0 0 ]\n");
	ASSERT_EQ(s.curves.size(), 1u);
	EXPECT_DOUBLE_EQ(s.curves[0].width0, 0.3);
	EXPECT_DOUBLE_EQ(s.curves[0].width1, 0.3);
}

TEST(FlattenTest, CurveControlPointsAreTransformedByTheCTM) {
	const FlatScene s = flattenSource(std::string("Translate 10 20 30\n") + kSingleSegmentCurve);
	ASSERT_EQ(s.curves.size(), 1u);
	EXPECT_DOUBLE_EQ(s.curves[0].cp[0], 10.0);
	EXPECT_DOUBLE_EQ(s.curves[0].cp[1], 20.0);
	EXPECT_DOUBLE_EQ(s.curves[0].cp[2], 30.0);
	// Fourth (last) control point: (3,0,0) + translate.
	EXPECT_DOUBLE_EQ(s.curves[0].cp[9], 13.0);
	EXPECT_DOUBLE_EQ(s.curves[0].cp[10], 20.0);
	EXPECT_DOUBLE_EQ(s.curves[0].cp[11], 30.0);
}

TEST(FlattenTest, CurveWithSevenControlPointsSplitsIntoTwoSegments) {
	// (N-1)/3 = 2 segments; pbrt-v4's own Curve::Create splitting convention
	// (shapes.cpp) - segment 2 shares its first control point with segment
	// 1's last.
	const FlatScene s = flattenSource(
		"Shape \"curve\"\n"
		"  \"point3 P\" [ 0 0 0  1 1 0  2 1 0  3 0 0  4 1 0  5 1 0  6 0 0 ]\n");
	ASSERT_EQ(s.curves.size(), 1u);
	EXPECT_EQ(s.curves[0].nSegments, 2);
	ASSERT_EQ(s.curves[0].cp.size(), 24u);
	// Segment 0's last point (index 3) and segment 1's first point (index 4)
	// are both (3,0,0), the shared endpoint.
	EXPECT_DOUBLE_EQ(s.curves[0].cp[9], 3.0);
	EXPECT_DOUBLE_EQ(s.curves[0].cp[12], 3.0);
}

TEST(FlattenTest, CurveWithInvalidControlPointCountWarnsAndSkips) {
	// 5 points: (5-1) % 3 != 0.
	const FlatScene s = flattenSource(
		"Shape \"curve\"\n"
		"  \"point3 P\" [ 0 0 0  1 1 0  2 1 0  3 0 0  4 0 0 ]\n");
	EXPECT_EQ(s.curves.size(), 0u);
	EXPECT_TRUE(warnedAbout(s, "control points"));
}

TEST(FlattenTest, CurveWithUnsupportedDegreeWarnsAndSkips) {
	// degree 5 (or any value other than 2/3) is genuinely unsupported -
	// unlike degree 2, which now builds for real (CurveDegree2Builds, below).
	const FlatScene s = flattenSource(
		std::string("Shape \"curve\" \"integer degree\" [ 5 ]\n") +
		"  \"point3 P\" [ 0 0 0  1 1 0  2 1 0  3 0 0  4 1 0  5 0 0 ]\n");
	EXPECT_EQ(s.curves.size(), 0u);
	EXPECT_TRUE(warnedAbout(s, "degree"));
}

TEST(FlattenTest, CurveDegree2Builds) {
	// Real quadratic (degree 2) Bezier curve support - exact degree
	// elevation to the cubic representation CurveShape's own intersection
	// math needs internally (curveDegreeElevateQuadratic's own comment).
	// 3 control points = exactly 1 quadratic segment (2*n+1 formula).
	const FlatScene s = flattenSource(
		std::string("Shape \"curve\" \"integer degree\" [ 2 ]\n") +
		"  \"point3 P\" [ 0 0 0  1 1 0  2 1 0 ]\n");
	ASSERT_EQ(s.curves.size(), 1u);
	EXPECT_EQ(s.curves[0].nSegments, 1);
	ASSERT_EQ(s.curves[0].cp.size(), 12u);
	// Exact degree-elevation identity check: the elevated cubic's endpoints
	// must equal the quadratic's own endpoints exactly (Q0=P0, Q3=P2).
	EXPECT_DOUBLE_EQ(s.curves[0].cp[0], 0.0);
	EXPECT_DOUBLE_EQ(s.curves[0].cp[1], 0.0);
	EXPECT_DOUBLE_EQ(s.curves[0].cp[9], 2.0);
	EXPECT_DOUBLE_EQ(s.curves[0].cp[10], 1.0);
	EXPECT_FALSE(warnedAbout(s, "degree"));
}

TEST(FlattenTest, CurveDegree2WrongPointCountWarnsAndSkips) {
	// A quadratic Bezier needs 2*n+1 points (3, 5, 7, ...) - 4 is valid for
	// neither a quadratic (would need 3 or 5) nor implicitly anything else
	// once degree is explicitly 2.
	const FlatScene s = flattenSource(
		std::string("Shape \"curve\" \"integer degree\" [ 2 ]\n") +
		"  \"point3 P\" [ 0 0 0  1 1 0  2 1 0  3 0 0 ]\n");
	EXPECT_EQ(s.curves.size(), 0u);
	EXPECT_TRUE(warnedAbout(s, "quadratic"));
}

TEST(FlattenTest, CurveBsplineBasisBuilds) {
	// Real "bspline" basis support - exact uniform-cubic-B-spline-to-Bezier
	// conversion (curveBsplineSegmentToBezierCubic's own comment). 5 control
	// points = exactly 2 segments (nSegments = numPoints - 3 formula).
	const FlatScene s = flattenSource(
		std::string("Shape \"curve\" \"string basis\" [ \"bspline\" ]\n") +
		"  \"point3 P\" [ 0 0 0  1 1 0  2 1 0  3 0 0  4 1 0 ]\n");
	ASSERT_EQ(s.curves.size(), 1u);
	EXPECT_EQ(s.curves[0].nSegments, 2);
	ASSERT_EQ(s.curves[0].cp.size(), 24u);
	EXPECT_FALSE(warnedAbout(s, "basis"));
}

TEST(FlattenTest, CurveBsplineTooFewPointsWarnsAndSkips) {
	const FlatScene s = flattenSource(
		std::string("Shape \"curve\" \"string basis\" [ \"bspline\" ]\n") +
		"  \"point3 P\" [ 0 0 0  1 1 0  2 1 0 ]\n");
	EXPECT_EQ(s.curves.size(), 0u);
	EXPECT_TRUE(warnedAbout(s, "B-spline"));
}

TEST(FlattenTest, CurveBsplineDegree2StillWarnsAndSkips) {
	// The one remaining disclosed scope cut - "bspline" is only supported
	// at degree 3 (AnimatedTriangleMesh-style deliberate cap, see the
	// curve-branch's own comment for why).
	const FlatScene s = flattenSource(
		std::string("Shape \"curve\" \"string basis\" [ \"bspline\" ] \"integer degree\" [ 2 ]\n") +
		"  \"point3 P\" [ 0 0 0  1 1 0  2 1 0  3 0 0 ]\n");
	EXPECT_EQ(s.curves.size(), 0u);
	EXPECT_TRUE(warnedAbout(s, "bspline"));
}

TEST(FlattenTest, CurveUnsupportedBasisWarnsAndSkips) {
	const FlatScene s = flattenSource(
		std::string("Shape \"curve\" \"string basis\" [ \"hermite\" ]\n") +
		"  \"point3 P\" [ 0 0 0  1 1 0  2 1 0  3 0 0 ]\n");
	EXPECT_EQ(s.curves.size(), 0u);
	EXPECT_TRUE(warnedAbout(s, "basis"));
}

TEST(FlattenTest, RibbonCurveWithoutNormalsWarnsAndSkips) {
	const FlatScene s = flattenSource(
		std::string("Shape \"curve\" \"string type\" [ \"ribbon\" ]\n") +
		"  \"point3 P\" [ 0 0 0  1 1 0  2 1 0  3 0 0 ]\n");
	EXPECT_EQ(s.curves.size(), 0u);
	EXPECT_TRUE(warnedAbout(s, "normal"));
}

TEST(FlattenTest, RibbonCurveWithNormalsBuilds) {
	const FlatScene s = flattenSource(
		"Shape \"curve\" \"string type\" [ \"ribbon\" ]\n"
		"  \"point3 P\" [ 0 0 0  1 1 0  2 1 0  3 0 0 ]\n"
		"  \"normal N\" [ 0 0 1  0 0 1 ]\n");
	ASSERT_EQ(s.curves.size(), 1u);
	EXPECT_EQ(s.curves[0].curveType, "ribbon");
	ASSERT_EQ(s.curves[0].n.size(), 6u);
	EXPECT_DOUBLE_EQ(s.curves[0].n[2], 1.0);
	EXPECT_DOUBLE_EQ(s.curves[0].n[5], 1.0);
}

TEST(FlattenTest, CurveRespectsMaterialAndAreaLight) {
	const FlatScene s = flattenSource(
		"Material \"diffuse\" \"rgb reflectance\" [ .5 .5 .5 ]\n"
		"AttributeBegin\n"
		"  AreaLightSource \"diffuse\" \"rgb L\" [ 5 5 5 ]\n"
		"  Shape \"curve\"\n"
		"    \"point3 P\" [ 0 0 0  1 1 0  2 1 0  3 0 0 ]\n"
		"AttributeEnd\n");
	ASSERT_EQ(s.curves.size(), 1u);
	EXPECT_EQ(s.curves[0].material, 0);
	EXPECT_EQ(s.curves[0].areaLight, 0);
}

// ===========================================================================
// PLY meshes
// ===========================================================================

TEST(FlattenTest, PlyMeshIsResolvedAndTransformed) {
	const MeshResolver res = [](const std::string &path,
								std::vector<float> &pos, std::vector<int> &idx,
								std::vector<float> &, std::vector<float> &) {
		if (path != "geometry/tri.ply") return false;
		pos = {0, 0, 0,  1, 0, 0,  0, 1, 0};
		idx = {0, 1, 2};
		return true;
	};
	const FlatScene s = flattenSource(
		"Translate 5 0 0\n"
		"Shape \"plymesh\" \"string filename\" [ \"geometry/tri.ply\" ]\n", res);
	ASSERT_EQ(s.triangles.size(), 1u);
	EXPECT_DOUBLE_EQ(s.triangles[0].v[0], 5.0);
	EXPECT_DOUBLE_EQ(s.triangles[0].v[3], 6.0);
	EXPECT_FALSE(s.triangles[0].hasUVs) << "the mock resolver supplied no UV";
}

TEST(FlattenTest, PlyMeshThreadsRealUVFromTheResolver) {
	const MeshResolver res = [](const std::string &path,
								std::vector<float> &pos, std::vector<int> &idx,
								std::vector<float> &uvs, std::vector<float> &) {
		if (path != "geometry/tri.ply") return false;
		pos = {0, 0, 0,  1, 0, 0,  0, 1, 0};
		idx = {0, 1, 2};
		uvs = {0, 0,  1, 0,  0, 1};
		return true;
	};
	const FlatScene s = flattenSource(
		"Shape \"plymesh\" \"string filename\" [ \"geometry/tri.ply\" ]\n", res);
	ASSERT_EQ(s.triangles.size(), 1u);
	ASSERT_TRUE(s.triangles[0].hasUVs);
	EXPECT_DOUBLE_EQ(s.triangles[0].uv[0], 0.0);
	EXPECT_DOUBLE_EQ(s.triangles[0].uv[1], 0.0);
	EXPECT_DOUBLE_EQ(s.triangles[0].uv[2], 1.0);
	EXPECT_DOUBLE_EQ(s.triangles[0].uv[3], 0.0);
	EXPECT_DOUBLE_EQ(s.triangles[0].uv[4], 0.0);
	EXPECT_DOUBLE_EQ(s.triangles[0].uv[5], 1.0);
}

TEST(FlattenTest, UnreadablePlyMeshWarnsRatherThanAborting) {
	const MeshResolver res = [](const std::string &, std::vector<float> &, std::vector<int> &,
								std::vector<float> &, std::vector<float> &) {
		return false;
	};
	const FlatScene s = flattenSource(
		"Shape \"plymesh\" \"string filename\" [ \"missing.ply\" ]\n" + std::string(kQuadMesh), res);
	EXPECT_TRUE(warnedAbout(s, "missing.ply"));
	EXPECT_EQ(s.triangles.size(), 2u) << "the rest of the scene still builds";
}

TEST(FlattenTest, PlyMeshWithoutAResolverIsReported) {
	const FlatScene s = flattenSource(
		"Shape \"plymesh\" \"string filename\" [ \"x.ply\" ]\n");
	EXPECT_TRUE(warnedAbout(s, "no mesh resolver"));
	EXPECT_TRUE(s.empty());
}

// ===========================================================================
// Malformed geometry
// ===========================================================================

TEST(FlattenTest, OutOfRangeFaceIndicesAreDroppedWithOneWarning) {
	const FlatScene s = flattenSource(
		"Shape \"trianglemesh\" \"integer indices\" [ 0 1 2  0 1 99 ]\n"
		"  \"point3 P\" [ 0 0 0  1 0 0  0 1 0 ]\n");
	EXPECT_EQ(s.triangles.size(), 1u) << "the valid face survives";
	EXPECT_TRUE(warnedAbout(s, "outside its vertex list"));
}

TEST(FlattenTest, TrailingIndicesThatDoNotFormATriangleAreReported) {
	const FlatScene s = flattenSource(
		"Shape \"trianglemesh\" \"integer indices\" [ 0 1 2  0 1 ]\n"
		"  \"point3 P\" [ 0 0 0  1 0 0  0 1 0 ]\n");
	EXPECT_EQ(s.triangles.size(), 1u);
	EXPECT_TRUE(warnedAbout(s, "multiple of 3"));
}

TEST(FlattenTest, MeshMissingItsParametersIsSkippedNotCrashed) {
	const FlatScene s = flattenSource("Shape \"trianglemesh\"\n");
	EXPECT_TRUE(s.empty());
	EXPECT_TRUE(warnedAbout(s, "missing its P or indices"));
}

TEST(FlattenTest, UnsupportedShapeTypesAreNamed) {
	// "hyperboloid" - not "cylinder"/"disk"/"cone"/"paraboloid", which this
	// loader now supports (see FlattenTest.MediumInterfaceAttachesTheNamedMedi
	// umToTheSphere's sibling tests above, pbrt_cpu_builder_tests.cpp's
	// disk/cylinder coverage, and the Cone/Paraboloid tests below).
	// Hyperboloid was deliberately scoped out (twisted ruled-surface math,
	// rare in practice) - see FlattenTest.Cone/Paraboloid tests for what IS
	// supported.
	const FlatScene s = flattenSource("Shape \"hyperboloid\" \"float radius\" [ 1 ]\n");
	EXPECT_TRUE(s.empty());
	EXPECT_TRUE(warnedAbout(s, "hyperboloid"));
}

// ===========================================================================
// Cone / Paraboloid (v1, geometry-only - see pbrt_flatten.h's Cone/Paraboloid
// comment block for scope)
// ===========================================================================

TEST(FlattenTest, ConeDefaultParamsMatchPbrt) {
	const FlatScene s = flattenSource("Shape \"cone\" \n");
	ASSERT_EQ(s.cones.size(), 1u);
	EXPECT_DOUBLE_EQ(s.cones[0].radius, 1.0);
	EXPECT_DOUBLE_EQ(s.cones[0].height, 1.0);
	EXPECT_DOUBLE_EQ(s.cones[0].phiMaxDeg, 360.0);
}

TEST(FlattenTest, ConeExplicitParamsAreRead) {
	const FlatScene s = flattenSource(
		"Shape \"cone\" \"float radius\" [ 1.5 ] \"float height\" [ 3 ] "
		"\"float phimax\" [ 180 ]\n");
	ASSERT_EQ(s.cones.size(), 1u);
	EXPECT_DOUBLE_EQ(s.cones[0].radius, 1.5);
	EXPECT_DOUBLE_EQ(s.cones[0].height, 3.0);
	EXPECT_DOUBLE_EQ(s.cones[0].phiMaxDeg, 180.0);
}

TEST(FlattenTest, ConeCarriesMaterialAndTransform) {
	const FlatScene s = flattenSource(
		"Material \"diffuse\" \"rgb reflectance\" [ 0.5 0.5 0.5 ]\n"
		"Translate 1 2 3\n"
		"Shape \"cone\" \"float radius\" [ 1 ] \"float height\" [ 1 ]\n");
	ASSERT_EQ(s.cones.size(), 1u);
	EXPECT_GE(s.cones[0].material, 0);
	EXPECT_DOUBLE_EQ(s.cones[0].xform[3], 1.0);
	EXPECT_DOUBLE_EQ(s.cones[0].xform[11], 3.0);
}

TEST(FlattenTest, ConeWithAreaLightSourceIsRealAndDoesNotWarn) {
	// Real AreaLightSource support now (Cone::areaLight's own comment) - no
	// longer the old geometry-only v1 scope this test's own name used to
	// describe.
	const FlatScene s = flattenSource(
		"AttributeBegin\n"
		"  AreaLightSource \"diffuse\" \"rgb L\" [ 1 1 1 ]\n"
		"  Shape \"cone\" \"float radius\" [ 1 ] \"float height\" [ 1 ]\n"
		"AttributeEnd\n");
	ASSERT_EQ(s.cones.size(), 1u);
	EXPECT_GE(s.cones[0].areaLight, 0);
	EXPECT_FALSE(warnedAbout(s, "AreaLightSource"));
}

TEST(FlattenTest, ConeWithMediumInterfaceIsRealAndDoesNotWarn) {
	// Real MediumInterface support now (Cone::medium's own comment) - no
	// longer the old geometry-only v1 scope this test's own name used to
	// describe.
	const FlatScene s = flattenSource(
		"MakeNamedMedium \"fog\" \"string type\" \"homogeneous\"\n"
		"AttributeBegin\n"
		"  MediumInterface \"fog\" \"\"\n"
		"  Shape \"cone\" \"float radius\" [ 1 ] \"float height\" [ 1 ]\n"
		"AttributeEnd\n");
	ASSERT_EQ(s.cones.size(), 1u);
	EXPECT_GE(s.cones[0].medium, 0);
	EXPECT_FALSE(warnedAbout(s, "MediumInterface"));
}

// ===========================================================================
// Accelerator - splitmethod/maxnodeprims resolution (pbrt_cpu_builder.h
// reads FlatScene::acceleratorSplitMethod to choose bvh_node vs
// bvh_aggregate_hittable.h's BvhTree wrapper; see that field's own comment)
// ===========================================================================

TEST(FlattenTest, AcceleratorDefaultsToSahWithNoWarning) {
	const FlatScene s = flattenSource("Shape \"sphere\"\n");
	EXPECT_EQ(s.acceleratorSplitMethod, "sah");
	EXPECT_EQ(s.acceleratorMaxNodePrims, 4);
	EXPECT_FALSE(warnedAbout(s, "Accelerator"));
}

TEST(FlattenTest, AcceleratorExplicitSahDoesNotWarn) {
	const FlatScene s = flattenSource(
		"Accelerator \"bvh\" \"string splitmethod\" \"sah\"\nShape \"sphere\"\n");
	EXPECT_EQ(s.acceleratorSplitMethod, "sah");
	EXPECT_FALSE(warnedAbout(s, "Accelerator"));
}

TEST(FlattenTest, AcceleratorHlbvhIsHonoredOnAStaticScene) {
	const FlatScene s = flattenSource(
		"Accelerator \"bvh\" \"string splitmethod\" \"hlbvh\" "
		"\"integer maxnodeprims\" [ 2 ]\nShape \"sphere\"\n");
	EXPECT_EQ(s.acceleratorSplitMethod, "hlbvh");
	EXPECT_EQ(s.acceleratorMaxNodePrims, 2);
	EXPECT_FALSE(warnedAbout(s, "Accelerator"));
}

TEST(FlattenTest, AcceleratorUnrecognizedTypeWarnsAndFallsBackToBvh) {
	// "grid"/"octree" - genuinely never-implemented historical pbrt
	// accelerator kinds (unlike "kdtree", pbrt-v4's own real second option -
	// see AcceleratorKdtreeIsAcceptedWithNoWarning below).
	const FlatScene s = flattenSource("Accelerator \"octree\"\nShape \"sphere\"\n");
	EXPECT_TRUE(warnedAbout(s, "octree"));
	EXPECT_EQ(s.acceleratorType, "bvh");
	// The type falls back, but splitmethod/maxnodeprims still resolve
	// normally from whatever params were given (none here -> defaults).
	EXPECT_EQ(s.acceleratorSplitMethod, "sah");
}

TEST(FlattenTest, AcceleratorDefaultsToBvhTypeWithNoWarning) {
	const FlatScene s = flattenSource("Shape \"sphere\"\n");
	EXPECT_EQ(s.acceleratorType, "bvh");
	EXPECT_FALSE(warnedAbout(s, "Accelerator"));
}

TEST(FlattenTest, AcceleratorKdtreeIsAcceptedWithNoWarning) {
	// pbrt-v4's real second Accelerator option (see src/shared/kd_tree.h's
	// own top comment - a genuine, complete port of pbrt-v4's own
	// KdTreeAggregate) - must resolve, not warn-and-fall-back like a truly
	// unrecognized type does (AcceleratorUnrecognizedTypeWarnsAndFallsBackToBvh
	// above).
	const FlatScene s = flattenSource("Accelerator \"kdtree\"\nShape \"sphere\"\n");
	EXPECT_EQ(s.acceleratorType, "kdtree");
	EXPECT_FALSE(warnedAbout(s, "Accelerator"));
}

TEST(FlattenTest, AcceleratorKdtreeParamsResolveToPbrtV4Defaults) {
	// pbrt-v4's real KdTreeAggregate::Create defaults (aggregates.cpp):
	// intersectcost=5, traversalcost=1, emptybonus=0.5, maxprims=1,
	// maxdepth=-1 - KdTreeAccelParams's own defaults (src/shared/kd_tree.h)
	// mirror these exactly (see that struct's own comment).
	const FlatScene s = flattenSource("Accelerator \"kdtree\"\nShape \"sphere\"\n");
	EXPECT_EQ(s.acceleratorKdParams.intersectCost, 5);
	EXPECT_EQ(s.acceleratorKdParams.traversalCost, 1);
	EXPECT_DOUBLE_EQ(s.acceleratorKdParams.emptyBonus, 0.5);
	EXPECT_EQ(s.acceleratorKdParams.maxPrims, 1);
	EXPECT_EQ(s.acceleratorKdParams.maxDepth, -1);
}

TEST(FlattenTest, AcceleratorKdtreeParamsAreReadWhenGiven) {
	const FlatScene s = flattenSource(
		"Accelerator \"kdtree\" \"integer intersectcost\" [ 8 ] "
		"\"integer traversalcost\" [ 2 ] \"float emptybonus\" [ 0.25 ] "
		"\"integer maxprims\" [ 3 ] \"integer maxdepth\" [ 12 ]\n"
		"Shape \"sphere\"\n");
	EXPECT_EQ(s.acceleratorKdParams.intersectCost, 8);
	EXPECT_EQ(s.acceleratorKdParams.traversalCost, 2);
	EXPECT_DOUBLE_EQ(s.acceleratorKdParams.emptyBonus, 0.25);
	EXPECT_EQ(s.acceleratorKdParams.maxPrims, 3);
	EXPECT_EQ(s.acceleratorKdParams.maxDepth, 12);
}

TEST(FlattenTest, AcceleratorKdtreeWithObjectMotionBlurFallsBackToBvh) {
	// kd_tree_hittable.h's KdTree wrapper has no ray-time channel, the exact
	// same gap as bvh_aggregate_hittable.h's own non-"sah" splitmethod
	// wrapper (see AcceleratorNonSahWithObjectMotionBlurFallsBackToSah
	// below) - a scene combining "kdtree" with a moving sphere must not
	// silently render that sphere frozen at time 0.
	const FlatScene s = flattenSource(
		"Accelerator \"kdtree\"\n"
		"ActiveTransform \"StartTime\"\n"
		"ActiveTransform \"EndTime\"\n"
		"Translate 2 0 0\n"
		"ActiveTransform \"All\"\n"
		"Shape \"sphere\" \"float radius\" [ 1 ]\n");
	ASSERT_EQ(s.spheres.size(), 1u);
	EXPECT_TRUE(s.spheres[0].center1[0] != s.spheres[0].center[0])
		<< "sanity check: the scene really does describe a moving sphere";
	EXPECT_EQ(s.acceleratorType, "bvh");
	EXPECT_TRUE(warnedAbout(s, "motion blur"));
}

TEST(FlattenTest, AcceleratorUnrecognizedSplitMethodWarnsAndFallsBackToSah) {
	const FlatScene s = flattenSource(
		"Accelerator \"bvh\" \"string splitmethod\" \"quadtree\"\nShape \"sphere\"\n");
	EXPECT_TRUE(warnedAbout(s, "quadtree"));
	EXPECT_EQ(s.acceleratorSplitMethod, "sah");
}

TEST(FlattenTest, AcceleratorNonSahWithObjectMotionBlurFallsBackToSah) {
	// bvh_aggregate_hittable.h's BvhTree wrapper has no ray-time channel
	// (see its own top comment) - a scene combining a non-default
	// splitmethod with a moving sphere must not silently render that
	// sphere frozen at time 0.
	const FlatScene s = flattenSource(
		"Accelerator \"bvh\" \"string splitmethod\" \"hlbvh\"\n"
		"ActiveTransform \"StartTime\"\n"
		"ActiveTransform \"EndTime\"\n"
		"Translate 2 0 0\n"
		"ActiveTransform \"All\"\n"
		"Shape \"sphere\" \"float radius\" [ 1 ]\n");
	ASSERT_EQ(s.spheres.size(), 1u);
	EXPECT_TRUE(s.spheres[0].center1[0] != s.spheres[0].center[0])
		<< "sanity check: the scene really does describe a moving sphere";
	EXPECT_EQ(s.acceleratorSplitMethod, "sah");
	EXPECT_TRUE(warnedAbout(s, "motion blur"));
}

TEST(FlattenTest, AcceleratorNonSahWithDiskCylinderMotionBlurFallsBackToSah) {
	// See AcceleratorNonSahWithObjectMotionBlurFallsBackToSah's own comment
	// just above - the identical gap, but for Disk/Cylinder, which gained
	// real per-ray-time motion blur in the same round this check was
	// extended to cover them (previously only scanned out.spheres).
	const FlatScene diskScene = flattenSource(
		"Accelerator \"bvh\" \"string splitmethod\" \"hlbvh\"\n"
		"ActiveTransform \"StartTime\"\n"
		"ActiveTransform \"EndTime\"\n"
		"Translate 2 0 0\n"
		"ActiveTransform \"All\"\n"
		"Shape \"disk\" \"float radius\" [ 1 ]\n");
	ASSERT_EQ(diskScene.disks.size(), 1u);
	EXPECT_TRUE(diskScene.disks[0].xform[3] != diskScene.disks[0].xformEnd[3])
		<< "sanity check: the scene really does describe a moving disk";
	EXPECT_EQ(diskScene.acceleratorSplitMethod, "sah");
	EXPECT_TRUE(warnedAbout(diskScene, "motion blur"));

	const FlatScene cylScene = flattenSource(
		"Accelerator \"bvh\" \"string splitmethod\" \"hlbvh\"\n"
		"ActiveTransform \"StartTime\"\n"
		"ActiveTransform \"EndTime\"\n"
		"Translate 2 0 0\n"
		"ActiveTransform \"All\"\n"
		"Shape \"cylinder\" \"float radius\" [ 1 ] \"float zmin\" [ -1 ] "
		"\"float zmax\" [ 1 ]\n");
	ASSERT_EQ(cylScene.cylinders.size(), 1u);
	EXPECT_TRUE(cylScene.cylinders[0].xform[3] != cylScene.cylinders[0].xformEnd[3])
		<< "sanity check: the scene really does describe a moving cylinder";
	EXPECT_EQ(cylScene.acceleratorSplitMethod, "sah");
	EXPECT_TRUE(warnedAbout(cylScene, "motion blur"));
}

TEST(FlattenTest, DiskObjectMotionBlurBakesXformEnd) {
	// Object motion blur (pbrt-v4 ActiveTransform "StartTime"/"EndTime") for
	// disk/cylinder - see pbrt_flatten::Disk::xformEnd's own comment. Mirrors
	// AcceleratorNonSahWithObjectMotionBlurFallsBackToSah's own sphere check
	// just above, for the disk/cylinder unbaked-transform representation.
	const FlatScene s = flattenSource(
		"ActiveTransform \"StartTime\"\n"
		"ActiveTransform \"EndTime\"\n"
		"Translate 2 0 0\n"
		"ActiveTransform \"All\"\n"
		"Shape \"disk\" \"float radius\" [ 1 ]\n");
	ASSERT_EQ(s.disks.size(), 1u);
	EXPECT_TRUE(s.disks[0].xform[3] != s.disks[0].xformEnd[3])
		<< "xform.tx=" << s.disks[0].xform[3] << " xformEnd.tx=" << s.disks[0].xformEnd[3];
}

TEST(FlattenTest, CylinderObjectMotionBlurBakesXformEnd) {
	// See DiskObjectMotionBlurBakesXformEnd's own comment - identical idiom.
	const FlatScene s = flattenSource(
		"ActiveTransform \"StartTime\"\n"
		"ActiveTransform \"EndTime\"\n"
		"Translate 2 0 0\n"
		"ActiveTransform \"All\"\n"
		"Shape \"cylinder\" \"float radius\" [ 1 ] \"float zmin\" [ -1 ] "
		"\"float zmax\" [ 1 ]\n");
	ASSERT_EQ(s.cylinders.size(), 1u);
	EXPECT_TRUE(s.cylinders[0].xform[3] != s.cylinders[0].xformEnd[3])
		<< "xform.tx=" << s.cylinders[0].xform[3] << " xformEnd.tx=" << s.cylinders[0].xformEnd[3];
}

TEST(FlattenTest, DiskAndCylinderWithoutActiveTransformHaveNoMotion) {
	// The default: a disk/cylinder that never sat inside an ActiveTransform
	// pair must render exactly as before this feature existed - xformEnd
	// equal to xform, bit for bit (see the bake sites' own "real inequality"
	// gate, matching Sphere::center1's identical convention).
	const FlatScene s = flattenSource(
		"Shape \"disk\" \"float radius\" [ 1 ]\n"
		"Shape \"cylinder\" \"float radius\" [ 1 ] \"float zmin\" [ -1 ] "
		"\"float zmax\" [ 1 ]\n");
	ASSERT_EQ(s.disks.size(), 1u);
	ASSERT_EQ(s.cylinders.size(), 1u);
	for (int i = 0; i < 16; ++i) {
		EXPECT_EQ(s.disks[0].xform[i], s.disks[0].xformEnd[i]) << "disk index " << i;
		EXPECT_EQ(s.cylinders[0].xform[i], s.cylinders[0].xformEnd[i]) << "cylinder index " << i;
	}
}

TEST(FlattenTest, TrianglemeshObjectMotionBlurPopulatesAnimatedTriangleMeshes) {
	// Object motion blur (pbrt-v4 ActiveTransform "StartTime"/"EndTime") for
	// trianglemesh/plymesh/loopsubdiv - see pbrt_flatten::
	// AnimatedTriangleMesh's own comment. Unlike Disk/Cylinder (an unbaked
	// xform/xformEnd pair on the same struct), a mesh explodes into many
	// Triangle entries, so real motion blur routes through a SEPARATE list
	// (animatedTriangleMeshes) of OBJECT-space triangles, and out.triangles
	// gets a GPU-only static-fallback duplicate instead (Triangle::
	// gpuOnlyStaticFallback's own comment) - both checked here.
	const FlatScene s = flattenSource(
		"ActiveTransform \"StartTime\"\n"
		"ActiveTransform \"EndTime\"\n"
		"Translate 2 0 0\n"
		"ActiveTransform \"All\"\n"
		+ std::string(kQuadMesh));

	ASSERT_EQ(s.animatedTriangleMeshes.size(), 1u);
	EXPECT_EQ(s.animatedTriangleMeshes[0].triangles.size(), 2u);
	// Object space: untouched by the Translate above (only xform/xformEnd
	// carry the placement now) - the first vertex is still exactly (0,0,0),
	// not (2,0,0).
	EXPECT_DOUBLE_EQ(s.animatedTriangleMeshes[0].triangles[0].v[0], 0.0);
	EXPECT_DOUBLE_EQ(s.animatedTriangleMeshes[0].triangles[0].v[1], 0.0);
	EXPECT_DOUBLE_EQ(s.animatedTriangleMeshes[0].triangles[0].v[2], 0.0);
	EXPECT_TRUE(s.animatedTriangleMeshes[0].xform[3] != s.animatedTriangleMeshes[0].xformEnd[3])
		<< "xform.tx=" << s.animatedTriangleMeshes[0].xform[3]
		<< " xformEnd.tx=" << s.animatedTriangleMeshes[0].xformEnd[3];

	// GPU-fallback duplicate: real world-space triangles at the StartTime
	// pose - identity here (the Translate above only applies between
	// ActiveTransform "EndTime" and "All", same convention the Disk/
	// Cylinder motion-blur tests already use) - flagged so CPU's own
	// builder skips them.
	ASSERT_EQ(s.triangles.size(), 2u);
	EXPECT_TRUE(s.triangles[0].gpuOnlyStaticFallback);
	EXPECT_DOUBLE_EQ(s.triangles[0].v[0], 0.0);
}

TEST(FlattenTest, TrianglemeshWithoutActiveTransformHasNoMotion) {
	// The default: a mesh that never sat inside an ActiveTransform pair
	// keeps using the plain, zero-overhead world-space bake - no
	// AnimatedTriangleMesh entry, no gpuOnlyStaticFallback duplicate.
	const FlatScene s = flattenSource(kQuadMesh);
	EXPECT_TRUE(s.animatedTriangleMeshes.empty());
	ASSERT_EQ(s.triangles.size(), 2u);
	EXPECT_FALSE(s.triangles[0].gpuOnlyStaticFallback);
}

TEST(FlattenTest, EmissiveTrianglemeshObjectMotionBlurFallsBackToStaticAndWarns) {
	// NEE sampling needs enumerable world-space geometry (this loader's own
	// established rule - see AnimatedTriangleMesh's own comment and the
	// ObjectInstance-definition precedent it cites) - an emissive mesh is
	// excluded from the animated path entirely, falling back to the exact
	// pre-existing static bake (StartTime pose only), with a warning rather
	// than silently dropping real motion.
	const FlatScene s = flattenSource(
		"AttributeBegin\n"
		"  AreaLightSource \"diffuse\" \"rgb L\" [ 1 1 1 ]\n"
		"  ActiveTransform \"StartTime\"\n"
		"  ActiveTransform \"EndTime\"\n"
		"  Translate 2 0 0\n"
		"  ActiveTransform \"All\"\n"
		+ std::string(kQuadMesh) +
		"AttributeEnd\n");
	EXPECT_TRUE(s.animatedTriangleMeshes.empty());
	ASSERT_EQ(s.triangles.size(), 2u);
	EXPECT_FALSE(s.triangles[0].gpuOnlyStaticFallback);
	EXPECT_GE(s.triangles[0].areaLight, 0);
	EXPECT_TRUE(warnedAbout(s, "emissive"));
}

TEST(FlattenTest, AcceleratorNonSahWithTrianglemeshMotionBlurFallsBackToSah) {
	// See AcceleratorNonSahWithObjectMotionBlurFallsBackToSah's own comment
	// (Sphere) - the identical non-SAH-BVH-has-no-ray-time-channel gap
	// applies to animated_transform_instance.h's mesh motion blur too.
	const FlatScene s = flattenSource(
		"Accelerator \"bvh\" \"string splitmethod\" \"hlbvh\"\n"
		"ActiveTransform \"StartTime\"\n"
		"ActiveTransform \"EndTime\"\n"
		"Translate 2 0 0\n"
		"ActiveTransform \"All\"\n"
		+ std::string(kQuadMesh));
	ASSERT_EQ(s.animatedTriangleMeshes.size(), 1u);
	EXPECT_EQ(s.acceleratorSplitMethod, "sah");
	EXPECT_TRUE(warnedAbout(s, "motion blur"));
}

TEST(FlattenTest, AcceleratorNonSahWithoutMotionBlurIsNotAffectedByTheMotionCheck) {
	const FlatScene s = flattenSource(
		"Accelerator \"bvh\" \"string splitmethod\" \"middle\"\n"
		"Shape \"sphere\" \"float radius\" [ 1 ]\n");
	ASSERT_EQ(s.spheres.size(), 1u);
	EXPECT_EQ(s.acceleratorSplitMethod, "middle");
	EXPECT_FALSE(warnedAbout(s, "motion blur"));
}

// ===========================================================================
// Bilinear-patch/curve object motion blur - mirrors the trianglemesh motion
// blur tests just above exactly, via AnimatedBilinearPatch/AnimatedCurve.
// ===========================================================================

TEST(FlattenTest, BilinearmeshObjectMotionBlurPopulatesAnimatedBilinearPatches) {
	const FlatScene s = flattenSource(
		"ActiveTransform \"StartTime\"\n"
		"ActiveTransform \"EndTime\"\n"
		"Translate 2 0 0\n"
		"ActiveTransform \"All\"\n"
		"Shape \"bilinearmesh\" \"point3 P\" [ -1 -1 0  1 -1 0  -1 1 0  1 1 0 ]\n");

	ASSERT_EQ(s.animatedBilinearPatches.size(), 1u);
	// Object space: untouched by the Translate above (only xform/xformEnd
	// carry the placement now) - the first corner is still exactly (-1,-1,0).
	EXPECT_DOUBLE_EQ(s.animatedBilinearPatches[0].p[0][0], -1.0);
	EXPECT_DOUBLE_EQ(s.animatedBilinearPatches[0].p[0][1], -1.0);
	EXPECT_DOUBLE_EQ(s.animatedBilinearPatches[0].p[0][2], 0.0);
	EXPECT_TRUE(s.animatedBilinearPatches[0].xform[3] != s.animatedBilinearPatches[0].xformEnd[3])
		<< "xform.tx=" << s.animatedBilinearPatches[0].xform[3]
		<< " xformEnd.tx=" << s.animatedBilinearPatches[0].xformEnd[3];

	// GPU-fallback duplicate: real world-space corners at the StartTime
	// pose (identity here), flagged so CPU's own builder skips it.
	ASSERT_EQ(s.bilinearPatches.size(), 1u);
	EXPECT_TRUE(s.bilinearPatches[0].gpuOnlyStaticFallback);
	EXPECT_DOUBLE_EQ(s.bilinearPatches[0].p[0][0], -1.0);
}

TEST(FlattenTest, BilinearmeshWithoutActiveTransformHasNoMotion) {
	const FlatScene s = flattenSource(
		"Shape \"bilinearmesh\" \"point3 P\" [ -1 -1 0  1 -1 0  -1 1 0  1 1 0 ]\n");
	EXPECT_TRUE(s.animatedBilinearPatches.empty());
	ASSERT_EQ(s.bilinearPatches.size(), 1u);
	EXPECT_FALSE(s.bilinearPatches[0].gpuOnlyStaticFallback);
}

TEST(FlattenTest, BilinearmeshWithFourIndicesPicksItsCornersByThem) {
	// "integer indices" naming the four points is still one patch: p00 p10 p01 p11 are taken from the points in the order of the indices (a floor written
	// 0 1 3 2 from a quad listed round its edge was read twisted when the indices were ignored).
	const FlatScene s = flattenSource(
		"Shape \"bilinearmesh\" \"integer indices\" [ 0 1 3 2 ] \"point3 P\" [ -1 -1 0  1 -1 0  1 1 0  -1 1 0 ]\n");
	ASSERT_EQ(s.bilinearPatches.size(), 1u);
	EXPECT_DOUBLE_EQ(s.bilinearPatches[0].p[2][0], -1.0);   // p01 = point 3
	EXPECT_DOUBLE_EQ(s.bilinearPatches[0].p[2][1], 1.0);
	EXPECT_DOUBLE_EQ(s.bilinearPatches[0].p[3][0], 1.0);    // p11 = point 2
	EXPECT_DOUBLE_EQ(s.bilinearPatches[0].p[3][1], 1.0);
	// more indices than one patch has are several patches, which this loader does not build
	const FlatScene t = flattenSource(
		"Shape \"bilinearmesh\" \"integer indices\" [ 0 1 3 2  0 1 3 2 ] \"point3 P\" [ -1 -1 0  1 -1 0  1 1 0  -1 1 0 ]\n");
	EXPECT_TRUE(t.bilinearPatches.empty());
}

TEST(FlattenMaterialTest, ScaleTextureOfAPlainColourIsAFlatColour) {
	// tex * scale, written out or through a constant texture; one that wraps an image is left to the image code.
	const FlatScene s = flattenSource(
		"Texture \"t\" \"spectrum\" \"scale\" \"rgb tex\" [ 0.3 0.2 0.1 ] \"float scale\" [ 2 ]\n"
		"Material \"diffuse\" \"texture reflectance\" [ \"t\" ]\n" + std::string(kQuadMesh));
	ASSERT_EQ(s.materials.size(), 1u);
	EXPECT_NEAR(s.materials[0].color[0], 0.6, 1e-12);
	EXPECT_NEAR(s.materials[0].color[1], 0.4, 1e-12);
	EXPECT_NEAR(s.materials[0].color[2], 0.2, 1e-12);
	EXPECT_FALSE(warnedAbout(s, "not supported"));
	const FlatScene u = flattenSource(
		"Texture \"c\" \"spectrum\" \"constant\" \"rgb value\" [ 0.5 0.5 0.5 ]\n"
		"Texture \"t\" \"spectrum\" \"scale\" \"texture tex\" [ \"c\" ] \"float scale\" [ 0.5 ]\n"
		"Material \"diffuse\" \"texture reflectance\" [ \"t\" ]\n" + std::string(kQuadMesh));
	ASSERT_EQ(u.materials.size(), 1u);
	EXPECT_NEAR(u.materials[0].color[1], 0.25, 1e-12);
}

TEST(FlattenMaterialTest, MixTextureOfPlainColoursLeavesTheirBlendAsTheFlatColour) {
	// A backend with no mix texture draws the flat colour: (1 - amount) * tex1 + amount * tex2.
	const FlatScene s = flattenSource(
		"Texture \"t\" \"spectrum\" \"mix\" \"rgb tex1\" [ 1 0 0 ] \"rgb tex2\" [ 0 1 0 ] \"float amount\" [ 0.25 ]\n"
		"Material \"diffuse\" \"texture reflectance\" [ \"t\" ]\n" + std::string(kQuadMesh));
	ASSERT_EQ(s.materials.size(), 1u);
	EXPECT_NEAR(s.materials[0].color[0], 0.75, 1e-12);
	EXPECT_NEAR(s.materials[0].color[1], 0.25, 1e-12);
	EXPECT_NEAR(s.materials[0].color[2], 0.0, 1e-12);
}

TEST(FlattenTest, EmissiveBilinearmeshObjectMotionBlurFallsBackToStaticAndWarns) {
	const FlatScene s = flattenSource(
		"AttributeBegin\n"
		"  AreaLightSource \"diffuse\" \"rgb L\" [ 1 1 1 ]\n"
		"  ActiveTransform \"StartTime\"\n"
		"  ActiveTransform \"EndTime\"\n"
		"  Translate 2 0 0\n"
		"  ActiveTransform \"All\"\n"
		"  Shape \"bilinearmesh\" \"point3 P\" [ -1 -1 0  1 -1 0  -1 1 0  1 1 0 ]\n"
		"AttributeEnd\n");
	EXPECT_TRUE(s.animatedBilinearPatches.empty());
	ASSERT_EQ(s.bilinearPatches.size(), 1u);
	EXPECT_FALSE(s.bilinearPatches[0].gpuOnlyStaticFallback);
	EXPECT_GE(s.bilinearPatches[0].areaLight, 0);
	EXPECT_TRUE(warnedAbout(s, "emissive"));
}

TEST(FlattenTest, CurveObjectMotionBlurPopulatesAnimatedCurves) {
	const FlatScene s = flattenSource(
		"ActiveTransform \"StartTime\"\n"
		"ActiveTransform \"EndTime\"\n"
		"Translate 2 0 0\n"
		"ActiveTransform \"All\"\n"
		"Shape \"curve\" \"point3 P\" [ 0 0 0  1 0 0  2 0 0  3 0 0 ]\n");

	ASSERT_EQ(s.animatedCurves.size(), 1u);
	EXPECT_EQ(s.animatedCurves[0].nSegments, 1);
	// Object space: untouched by the Translate above.
	EXPECT_DOUBLE_EQ(s.animatedCurves[0].cp[0], 0.0);
	EXPECT_TRUE(s.animatedCurves[0].xform[3] != s.animatedCurves[0].xformEnd[3])
		<< "xform.tx=" << s.animatedCurves[0].xform[3]
		<< " xformEnd.tx=" << s.animatedCurves[0].xformEnd[3];

	ASSERT_EQ(s.curves.size(), 1u);
	EXPECT_TRUE(s.curves[0].gpuOnlyStaticFallback);
	EXPECT_DOUBLE_EQ(s.curves[0].cp[0], 0.0);
}

TEST(FlattenTest, CurveWithoutActiveTransformHasNoMotion) {
	const FlatScene s = flattenSource(
		"Shape \"curve\" \"point3 P\" [ 0 0 0  1 0 0  2 0 0  3 0 0 ]\n");
	EXPECT_TRUE(s.animatedCurves.empty());
	ASSERT_EQ(s.curves.size(), 1u);
	EXPECT_FALSE(s.curves[0].gpuOnlyStaticFallback);
}

TEST(FlattenTest, EmissiveCurveObjectMotionBlurFallsBackToStaticAndWarns) {
	const FlatScene s = flattenSource(
		"AttributeBegin\n"
		"  AreaLightSource \"diffuse\" \"rgb L\" [ 1 1 1 ]\n"
		"  ActiveTransform \"StartTime\"\n"
		"  ActiveTransform \"EndTime\"\n"
		"  Translate 2 0 0\n"
		"  ActiveTransform \"All\"\n"
		"  Shape \"curve\" \"point3 P\" [ 0 0 0  1 0 0  2 0 0  3 0 0 ]\n"
		"AttributeEnd\n");
	EXPECT_TRUE(s.animatedCurves.empty());
	ASSERT_EQ(s.curves.size(), 1u);
	EXPECT_FALSE(s.curves[0].gpuOnlyStaticFallback);
	EXPECT_GE(s.curves[0].areaLight, 0);
	EXPECT_TRUE(warnedAbout(s, "emissive"));
}

TEST(FlattenTest, RibbonCurveObjectMotionBlurFallsBackToStaticAndWarns) {
	// AnimatedCurve's own comment: ribbon-type curves are excluded from
	// motion blur (their per-segment shading normals have no per-ray-time
	// transform yet) - a separate exclusion from the emissive one above.
	const FlatScene s = flattenSource(
		"ActiveTransform \"StartTime\"\n"
		"ActiveTransform \"EndTime\"\n"
		"Translate 2 0 0\n"
		"ActiveTransform \"All\"\n"
		"Shape \"curve\" \"point3 P\" [ 0 0 0  1 0 0  2 0 0  3 0 0 ] "
		"\"string type\" [ \"ribbon\" ] "
		"\"normal N\" [ 0 1 0  0 1 0 ]\n");
	EXPECT_TRUE(s.animatedCurves.empty());
	ASSERT_EQ(s.curves.size(), 1u);
	EXPECT_FALSE(s.curves[0].gpuOnlyStaticFallback);
	EXPECT_EQ(s.curves[0].curveType, "ribbon");
	EXPECT_TRUE(warnedAbout(s, "ribbon"));
}

TEST(FlattenTest, AcceleratorNonSahWithBilinearmeshMotionBlurFallsBackToSah) {
	const FlatScene s = flattenSource(
		"Accelerator \"bvh\" \"string splitmethod\" \"hlbvh\"\n"
		"ActiveTransform \"StartTime\"\n"
		"ActiveTransform \"EndTime\"\n"
		"Translate 2 0 0\n"
		"ActiveTransform \"All\"\n"
		"Shape \"bilinearmesh\" \"point3 P\" [ -1 -1 0  1 -1 0  -1 1 0  1 1 0 ]\n");
	ASSERT_EQ(s.animatedBilinearPatches.size(), 1u);
	EXPECT_EQ(s.acceleratorSplitMethod, "sah");
	EXPECT_TRUE(warnedAbout(s, "motion blur"));
}

TEST(FlattenTest, AcceleratorNonSahWithCurveMotionBlurFallsBackToSah) {
	const FlatScene s = flattenSource(
		"Accelerator \"bvh\" \"string splitmethod\" \"hlbvh\"\n"
		"ActiveTransform \"StartTime\"\n"
		"ActiveTransform \"EndTime\"\n"
		"Translate 2 0 0\n"
		"ActiveTransform \"All\"\n"
		"Shape \"curve\" \"point3 P\" [ 0 0 0  1 0 0  2 0 0  3 0 0 ]\n");
	ASSERT_EQ(s.animatedCurves.size(), 1u);
	EXPECT_EQ(s.acceleratorSplitMethod, "sah");
	EXPECT_TRUE(warnedAbout(s, "motion blur"));
}

TEST(FlattenTest, ParaboloidDefaultParamsMatchPbrt) {
	const FlatScene s = flattenSource("Shape \"paraboloid\" \n");
	ASSERT_EQ(s.paraboloids.size(), 1u);
	EXPECT_DOUBLE_EQ(s.paraboloids[0].radius, 1.0);
	EXPECT_DOUBLE_EQ(s.paraboloids[0].zMin, 0.0);
	EXPECT_DOUBLE_EQ(s.paraboloids[0].zMax, 1.0);
	EXPECT_DOUBLE_EQ(s.paraboloids[0].phiMaxDeg, 360.0);
}

TEST(FlattenTest, ParaboloidExplicitParamsAreRead) {
	const FlatScene s = flattenSource(
		"Shape \"paraboloid\" \"float radius\" [ 1.5 ] \"float zmin\" [ 0 ] "
		"\"float zmax\" [ 2 ] \"float phimax\" [ 180 ]\n");
	ASSERT_EQ(s.paraboloids.size(), 1u);
	EXPECT_DOUBLE_EQ(s.paraboloids[0].radius, 1.5);
	EXPECT_DOUBLE_EQ(s.paraboloids[0].zMin, 0.0);
	EXPECT_DOUBLE_EQ(s.paraboloids[0].zMax, 2.0);
	EXPECT_DOUBLE_EQ(s.paraboloids[0].phiMaxDeg, 180.0);
}

TEST(FlattenTest, ParaboloidWithAreaLightSourceIsRealAndDoesNotWarn) {
	// See ConeWithAreaLightSourceIsRealAndDoesNotWarn's own comment -
	// identical, for Paraboloid::areaLight.
	const FlatScene s = flattenSource(
		"AttributeBegin\n"
		"  AreaLightSource \"diffuse\" \"rgb L\" [ 1 1 1 ]\n"
		"  Shape \"paraboloid\" \"float radius\" [ 1 ]\n"
		"AttributeEnd\n");
	ASSERT_EQ(s.paraboloids.size(), 1u);
	EXPECT_GE(s.paraboloids[0].areaLight, 0);
	EXPECT_FALSE(warnedAbout(s, "AreaLightSource"));
}

TEST(FlattenTest, ParaboloidWithMediumInterfaceIsRealAndDoesNotWarn) {
	// See ConeWithMediumInterfaceIsRealAndDoesNotWarn's own comment -
	// identical, for Paraboloid::medium.
	const FlatScene s = flattenSource(
		"MakeNamedMedium \"fog\" \"string type\" \"homogeneous\"\n"
		"AttributeBegin\n"
		"  MediumInterface \"fog\" \"\"\n"
		"  Shape \"paraboloid\" \"float radius\" [ 1 ]\n"
		"AttributeEnd\n");
	ASSERT_EQ(s.paraboloids.size(), 1u);
	EXPECT_GE(s.paraboloids[0].medium, 0);
	EXPECT_FALSE(warnedAbout(s, "MediumInterface"));
}

TEST(FlattenTest, ParserWarningsAreCarriedThrough) {
	// The caller gets one list, not two - it should not have to check both the
	// parse result and the flatten result to learn what was approximated.
	// "Accelerator \"bvh\"" used to be this test's own example - it's real,
	// fully-supported input now (see the Accelerator tests above), so this
	// uses a directive pbrt-v4 has no such grammar for at all instead.
	const FlatScene s = flattenSource(
		"NotARealDirective \"foo\"\n" + std::string(kQuadMesh));
	EXPECT_TRUE(warnedAbout(s, "NotARealDirective"));
}

// ===========================================================================
// Shape "bilinearmesh"
// ===========================================================================
// sportscar-area-lights.pbrt authors all 5 of its studio light panels as
// bilinear patches - dropping the shape (the old behaviour) loses 100% of
// that scene's light geometry and renders pure black.

TEST(FlattenBilinearMeshTest, FourPointFormPopulatesCorners) {
	const FlatScene s = flattenSource(
		"Shape \"bilinearmesh\" \"point3 P\" [ 0 0 0  1 0 0  0 1 0  1 1 0 ]\n");
	ASSERT_EQ(s.bilinearPatches.size(), 1u);
	const pbrt_flatten::BilinearPatch &bp = s.bilinearPatches[0];
	EXPECT_DOUBLE_EQ(bp.p[0][0], 0.0); EXPECT_DOUBLE_EQ(bp.p[0][1], 0.0);
	EXPECT_DOUBLE_EQ(bp.p[1][0], 1.0); EXPECT_DOUBLE_EQ(bp.p[1][1], 0.0);
	EXPECT_DOUBLE_EQ(bp.p[2][0], 0.0); EXPECT_DOUBLE_EQ(bp.p[2][1], 1.0);
	EXPECT_DOUBLE_EQ(bp.p[3][0], 1.0); EXPECT_DOUBLE_EQ(bp.p[3][1], 1.0);
}

TEST(FlattenBilinearMeshTest, TransformIsBakedIntoEveryCorner) {
	const FlatScene s = flattenSource(
		"Translate 10 20 30\n"
		"Shape \"bilinearmesh\" \"point3 P\" [ 0 0 0  1 0 0  0 1 0  1 1 0 ]\n");
	ASSERT_EQ(s.bilinearPatches.size(), 1u);
	const pbrt_flatten::BilinearPatch &bp = s.bilinearPatches[0];
	EXPECT_DOUBLE_EQ(bp.p[0][0], 10.0);
	EXPECT_DOUBLE_EQ(bp.p[0][1], 20.0);
	EXPECT_DOUBLE_EQ(bp.p[0][2], 30.0);
	EXPECT_DOUBLE_EQ(bp.p[1][0], 11.0) << "second corner translated too";
}

TEST(FlattenBilinearMeshTest, AreaLightSourceMarksItEmissive) {
	const FlatScene s = flattenSource(
		"AttributeBegin\n"
		"  AreaLightSource \"diffuse\" \"rgb L\" [ 5 5 5 ]\n"
		"  Shape \"bilinearmesh\" \"point3 P\" [ 0 0 0  1 0 0  0 1 0  1 1 0 ]\n"
		"AttributeEnd\n"
		"Shape \"bilinearmesh\" \"point3 P\" [ 0 0 5  1 0 5  0 1 5  1 1 5 ]\n");
	ASSERT_EQ(s.bilinearPatches.size(), 2u);
	EXPECT_GE(s.bilinearPatches[0].areaLight, 0) << "inside the AreaLightSource scope";
	EXPECT_EQ(s.bilinearPatches[1].areaLight, -1) << "outside it";
}

TEST(FlattenBilinearMeshTest, MultiPatchIndexedFormFallsThroughToTheGenericWarning) {
	// Only the single-patch "point3 P" (4 points) form is built - see
	// BilinearPatch's own comment on why the multi-patch "integer indices"
	// form is out of scope. It must not silently build a wrong/empty patch.
	const FlatScene s = flattenSource(
		"Shape \"bilinearmesh\" \"integer indices\" [ 0 1 2 3 ]\n"
		"  \"point3 P\" [ 0 0 0  1 0 0  0 1 0  1 1 0  1 1 1  0 1 1 ]\n");
	EXPECT_TRUE(s.bilinearPatches.empty());
	EXPECT_TRUE(warnedAbout(s, "bilinearmesh"));
}


// FlatScene::shapeRanges: which primitives each top-level Shape made, for the Live Preview's object picking and moving.
TEST(PbrtFlattenShapeRangesTest, EachTopLevelShapeGetsItsOwnRangeOfPrimitives) {
	const FlatScene s = flattenSource(
		"WorldBegin\n"
		"Shape \"sphere\" \"float radius\" [ 1 ]\n"
		"AttributeBegin\n  Translate 5 0 0\n" + std::string(kQuadMesh) + "AttributeEnd\n"
		"Shape \"sphere\" \"float radius\" [ 2 ]\n"
		"Shape \"disk\" \"float radius\" [ 1 ]\n"
		+ std::string(kQuadMesh));
	ASSERT_EQ(s.shapeRanges.size(), 5u);
	EXPECT_EQ(s.shapeRanges[0].type, "sphere");
	EXPECT_EQ(s.shapeRanges[0].sphereBegin, 0u);
	EXPECT_EQ(s.shapeRanges[0].sphereEnd, 1u);
	EXPECT_EQ(s.shapeRanges[1].type, "trianglemesh");
	EXPECT_EQ(s.shapeRanges[1].triBegin, 0u);
	EXPECT_EQ(s.shapeRanges[1].triEnd, 2u);   // a quad is two triangles
	EXPECT_EQ(s.shapeRanges[2].sphereBegin, 1u);
	EXPECT_EQ(s.shapeRanges[2].sphereEnd, 2u);
	EXPECT_EQ(s.shapeRanges[3].type, "disk");
	EXPECT_EQ(s.shapeRanges[3].diskEnd - s.shapeRanges[3].diskBegin, 1u);
	EXPECT_EQ(s.shapeRanges[4].triBegin, 2u);
	EXPECT_EQ(s.shapeRanges[4].triEnd, 4u);
	// The ranges tile the primitive arrays exactly: nothing is left over or counted twice.
	EXPECT_EQ(s.triangles.size(), 4u);
	EXPECT_EQ(s.spheres.size(), 2u);
}

TEST(PbrtFlattenShapeRangesTest, AShapeThatMakesNothingGetsNoRange) {
	const FlatScene s = flattenSource("WorldBegin\nShape \"sphere\" \"float radius\" [ 1 ]\nShape \"notashape\"\n");
	EXPECT_EQ(s.shapeRanges.size(), 1u);
}

// pbrt's own spelling of a normal map: a material parameter naming an image, not a texture. The loader puts it in the same slot a displacement texture's picture goes (the renderers tell a
// normal map from a height map by the picture), and a displacement texture on the same material wins.
TEST(PbrtFlattenNormalMapTest, AStringNormalMapParameterBecomesTheMaterialsDisplacementPicture) {
	const FlatScene s = flattenSource(std::string("WorldBegin\nMaterial \"diffuse\" \"string normalmap\" [ \"bumps.png\" ]\n") + kQuadMesh);
	ASSERT_FALSE(s.materials.empty());
	EXPECT_EQ(s.materials.back().displacementTextureFilename, "bumps.png");
	EXPECT_DOUBLE_EQ(s.materials.back().displacementScale, 1.0);
	const FlatScene plain = flattenSource(std::string("WorldBegin\nMaterial \"diffuse\"\n") + kQuadMesh);
	EXPECT_TRUE(plain.materials.back().displacementTextureFilename.empty());
}

TEST(PbrtFlattenNormalMapTest, ADisplacementTextureOnTheSameMaterialWins) {
	const FlatScene s = flattenSource(std::string("Texture \"d\" \"float\" \"imagemap\" \"string filename\" [ \"height.png\" ]\nWorldBegin\n") +
	                                  "Material \"diffuse\" \"texture displacement\" \"d\" \"string normalmap\" [ \"normals.png\" ]\n" + kQuadMesh);
	EXPECT_EQ(s.materials.back().displacementTextureFilename, "height.png");
}
