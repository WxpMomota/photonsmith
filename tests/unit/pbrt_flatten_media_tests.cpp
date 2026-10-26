/**
 * @file pbrt_flatten_media_tests.cpp
 * @brief pbrt_flatten tests: Hair, MakeNamedMedium / MediumInterface, nanovdb, the camera medium.
 * The helpers are in pbrt_flatten_test_helpers.h; this file is one part of what used to be a single pbrt_flatten_tests.cpp.
 */

#include "pbrt_flatten_test_helpers.h"

// ===========================================================================
// Material "hair"
// ===========================================================================

TEST(FlattenTest, HairMaterialWithDirectSigmaA) {
	const FlatScene s = flattenSource(
		"Material \"hair\" \"rgb sigma_a\" [ 0.15 0.06 0.03 ] "
		"\"float beta_m\" [ 0.25 ] \"float beta_n\" [ 0.35 ] \"float alpha\" [ 3 ]\n"
		"Shape \"sphere\"\n");
	ASSERT_EQ(s.materials.size(), 1u);
	EXPECT_EQ(s.materials[0].kind, MaterialKind::Hair);
	EXPECT_DOUBLE_EQ(s.materials[0].sigma_a[0], 0.15);
	EXPECT_DOUBLE_EQ(s.materials[0].sigma_a[1], 0.06);
	EXPECT_DOUBLE_EQ(s.materials[0].sigma_a[2], 0.03);
	EXPECT_DOUBLE_EQ(s.materials[0].betaM, 0.25);
	EXPECT_DOUBLE_EQ(s.materials[0].betaN, 0.35);
	EXPECT_DOUBLE_EQ(s.materials[0].alphaDeg, 3.0);
	// pbrt-v4's own Hair-specific eta default, not the generic 1.5.
	EXPECT_DOUBLE_EQ(s.materials[0].ior, 1.55);
}

TEST(FlattenTest, HairMaterialWithEumelaninPheomelanin) {
	// sigma_a = ce*(0.419,0.697,1.37) + cp*(0.187,0.4,1.05) - pbrt-v4's own
	// HairBxDF::SigmaAFromConcentration (bxdfs.cpp).
	const FlatScene s = flattenSource(
		"Material \"hair\" \"float eumelanin\" [ 1.0 ] \"float pheomelanin\" [ 0.5 ]\n"
		"Shape \"sphere\"\n");
	ASSERT_EQ(s.materials.size(), 1u);
	EXPECT_NEAR(s.materials[0].sigma_a[0], 1.0 * 0.419 + 0.5 * 0.187, 1e-9);
	EXPECT_NEAR(s.materials[0].sigma_a[1], 1.0 * 0.697 + 0.5 * 0.4, 1e-9);
	EXPECT_NEAR(s.materials[0].sigma_a[2], 1.0 * 1.37 + 0.5 * 1.05, 1e-9);
}

TEST(FlattenTest, HairMaterialWithNothingSpecifiedUsesDefaultBrown) {
	// pbrt-v4's own default: SigmaAFromConcentration(1.3, 0.).
	const FlatScene s = flattenSource("Material \"hair\"\nShape \"sphere\"\n");
	ASSERT_EQ(s.materials.size(), 1u);
	EXPECT_NEAR(s.materials[0].sigma_a[0], 1.3 * 0.419, 1e-9);
	EXPECT_NEAR(s.materials[0].sigma_a[1], 1.3 * 0.697, 1e-9);
	EXPECT_NEAR(s.materials[0].sigma_a[2], 1.3 * 1.37, 1e-9);
}

namespace {
// pbrt-v4 HairBxDF::SigmaAFromReflectance (bxdfs.cpp) - mirrors
// pbrt_flatten.h's own sigmaAFromReflectanceChannel lambda exactly, so this
// test verifies the loader against the real formula rather than a magic
// literal.
double sigmaAFromReflectanceChannelRef(double c, double bn) {
	c = c < 1e-4 ? 1e-4 : (c > 1.0 - 1e-4 ? 1.0 - 1e-4 : c);
	const double bn2 = bn * bn, bn3 = bn2 * bn, bn4 = bn3 * bn, bn5 = bn4 * bn;
	const double denom = 5.969 - 0.215 * bn + 2.532 * bn2
		- 10.73 * bn3 + 5.574 * bn4 + 0.245 * bn5;
	const double x = std::log(c) / denom;
	return x * x;
}
} // namespace

TEST(FlattenTest, HairMaterialWithReflectanceInvertsToSigmaA) {
	// beta_n stays at its 0.3 default - matches pbrt-v4's own priority
	// (reflectance wins over eumelanin/pheomelanin, and no longer falls back
	// to the default brown preset the way it used to before this loader
	// implemented the closed-form inversion).
	const FlatScene s = flattenSource(
		"Material \"hair\" \"rgb reflectance\" [ 0.5 0.3 0.2 ]\n"
		"Shape \"sphere\"\n");
	ASSERT_EQ(s.materials.size(), 1u);
	EXPECT_NEAR(s.materials[0].sigma_a[0], sigmaAFromReflectanceChannelRef(0.5, 0.3), 1e-9);
	EXPECT_NEAR(s.materials[0].sigma_a[1], sigmaAFromReflectanceChannelRef(0.3, 0.3), 1e-9);
	EXPECT_NEAR(s.materials[0].sigma_a[2], sigmaAFromReflectanceChannelRef(0.2, 0.3), 1e-9);
	EXPECT_FALSE(warnedAbout(s, "reflectance"));
}

TEST(FlattenTest, HairMaterialWithReflectanceAndBetaNUsesTheGivenBetaN) {
	// beta_n must be read BEFORE sigma_a resolution, since the reflectance
	// inversion formula depends on it - this pins that ordering.
	const FlatScene s = flattenSource(
		"Material \"hair\" \"rgb reflectance\" [ 0.5 0.3 0.2 ] \"float beta_n\" [ 0.6 ]\n"
		"Shape \"sphere\"\n");
	ASSERT_EQ(s.materials.size(), 1u);
	EXPECT_NEAR(s.materials[0].sigma_a[0], sigmaAFromReflectanceChannelRef(0.5, 0.6), 1e-9);
	// Sanity: using the WRONG (default 0.3) beta_n would give a visibly
	// different value, so this also proves beta_n is actually threaded
	// through rather than silently ignored.
	EXPECT_NE(s.materials[0].sigma_a[0], sigmaAFromReflectanceChannelRef(0.5, 0.3));
}

TEST(FlattenTest, HairMaterialWithReflectanceAndEumelaninWarnsReflectanceWins) {
	const FlatScene s = flattenSource(
		"Material \"hair\" \"rgb reflectance\" [ 0.5 0.3 0.2 ] \"float eumelanin\" [ 2.0 ]\n"
		"Shape \"sphere\"\n");
	ASSERT_EQ(s.materials.size(), 1u);
	EXPECT_NEAR(s.materials[0].sigma_a[0], sigmaAFromReflectanceChannelRef(0.5, 0.3), 1e-9);
	EXPECT_TRUE(warnedAbout(s, "reflectance"));
}

TEST(FlattenTest, HairMaterialDefaultBetaAndAlphaMatchHairMaterialHDefaults) {
	const FlatScene s = flattenSource("Material \"hair\"\nShape \"sphere\"\n");
	ASSERT_EQ(s.materials.size(), 1u);
	EXPECT_DOUBLE_EQ(s.materials[0].betaM, 0.3);
	EXPECT_DOUBLE_EQ(s.materials[0].betaN, 0.3);
	EXPECT_DOUBLE_EQ(s.materials[0].alphaDeg, 2.0);
}

// ===========================================================================
// MakeNamedMedium / MediumInterface
// ===========================================================================

TEST(FlattenTest, MediumInterfaceAttachesTheNamedMediumToTheSphere) {
	const FlatScene s = flattenSource(
		"MakeNamedMedium \"fog\" \"string type\" \"homogeneous\"\n"
		"  \"rgb sigma_a\" [ 0.1 0.2 0.3 ] \"rgb sigma_s\" [ 1 2 3 ] \"float scale\" [ 2 ]\n"
		"AttributeBegin\n"
		"  MediumInterface \"fog\" \"\"\n"
		"  Shape \"sphere\" \"float radius\" [ 1 ]\n"
		"AttributeEnd\n");
	ASSERT_EQ(s.spheres.size(), 1u);
	ASSERT_EQ(s.media.size(), 1u);
	ASSERT_EQ(s.spheres[0].medium, 0)
		<< "the sphere should resolve to media[0], the only declared medium";

	// scale multiplies both coefficients (pbrt-v4 semantics), so 0.1*2=0.2 etc.
	EXPECT_DOUBLE_EQ(s.media[0].sigma_a[0], 0.2);
	EXPECT_DOUBLE_EQ(s.media[0].sigma_a[1], 0.4);
	EXPECT_DOUBLE_EQ(s.media[0].sigma_a[2], 0.6);
	EXPECT_DOUBLE_EQ(s.media[0].sigma_s[0], 2.0);
	EXPECT_DOUBLE_EQ(s.media[0].sigma_s[1], 4.0);
	EXPECT_DOUBLE_EQ(s.media[0].sigma_s[2], 6.0);
}

TEST(FlattenTest, HomogeneousMediumLeAndLescaleAreThreadedThrough) {
	const FlatScene s = flattenSource(
		"MakeNamedMedium \"fire\" \"string type\" \"homogeneous\"\n"
		"  \"rgb Le\" [ 1 0.5 0.1 ] \"float Lescale\" [ 2 ]\n"
		"AttributeBegin\n"
		"  MediumInterface \"fire\" \"\"\n"
		"  Shape \"sphere\" \"float radius\" [ 1 ]\n"
		"AttributeEnd\n");
	ASSERT_EQ(s.media.size(), 1u);
	EXPECT_DOUBLE_EQ(s.media[0].Le[0], 2.0);
	EXPECT_DOUBLE_EQ(s.media[0].Le[1], 1.0);
	EXPECT_DOUBLE_EQ(s.media[0].Le[2], 0.2);
}

TEST(FlattenTest, HomogeneousMediumDefaultsToNoEmission) {
	const FlatScene s = flattenSource(
		"MakeNamedMedium \"fog\" \"string type\" \"homogeneous\"\n"
		"AttributeBegin\n"
		"  MediumInterface \"fog\" \"\"\n"
		"  Shape \"sphere\" \"float radius\" [ 1 ]\n"
		"AttributeEnd\n");
	ASSERT_EQ(s.media.size(), 1u);
	EXPECT_DOUBLE_EQ(s.media[0].Le[0], 0.0);
	EXPECT_DOUBLE_EQ(s.media[0].Le[1], 0.0);
	EXPECT_DOUBLE_EQ(s.media[0].Le[2], 0.0);
}

TEST(FlattenTest, CloudMediumLeIsDroppedAndWarns) {
	// Only "homogeneous" supports emission this round (Medium::Le's own
	// comment) - cloud/rgbgrid/uniformgrid's real per-voxel emission is a
	// separate, deferred feature.
	const FlatScene s = flattenSource(
		"MakeNamedMedium \"puff\" \"string type\" [ \"cloud\" ]\n"
		"  \"rgb Le\" [ 1 1 1 ]\n"
		"AttributeBegin\n"
		"  MediumInterface \"puff\" \"\"\n"
		"  Shape \"sphere\" \"float radius\" [ 1 ]\n"
		"AttributeEnd\n");
	ASSERT_EQ(s.media.size(), 1u);
	EXPECT_TRUE(warnedAbout(s, "Le"));
}

TEST(FlattenTest, SphereWithNoMediumInterfaceIsVacuum) {
	const FlatScene s = flattenSource("Shape \"sphere\" \"float radius\" [ 1 ]\n");
	ASSERT_EQ(s.spheres.size(), 1u);
	EXPECT_EQ(s.spheres[0].medium, -1);
}

TEST(FlattenTest, MediumInterfaceIsScopedByAttributeEnd) {
	// A sphere declared AFTER AttributeEnd must not inherit the medium the
	// first sphere picked up inside the block - the same scoping already
	// proven for materialIndex, now exercised for insideMedium.
	const FlatScene s = flattenSource(
		"MakeNamedMedium \"fog\" \"string type\" \"homogeneous\"\n"
		"AttributeBegin\n"
		"  MediumInterface \"fog\" \"\"\n"
		"  Shape \"sphere\" \"float radius\" [ 1 ]\n"
		"AttributeEnd\n"
		"Shape \"sphere\" \"float radius\" [ 1 ]\n");
	ASSERT_EQ(s.spheres.size(), 2u);
	EXPECT_EQ(s.spheres[0].medium, 0);
	EXPECT_EQ(s.spheres[1].medium, -1)
		<< "the second sphere is outside the AttributeBegin/End block";
}

TEST(FlattenTest, UnresolvedMediumNameIsVacuumAndWarns) {
	const FlatScene s = flattenSource(
		"AttributeBegin\n"
		"  MediumInterface \"ghost\" \"\"\n"
		"  Shape \"sphere\" \"float radius\" [ 1 ]\n"
		"AttributeEnd\n");
	ASSERT_EQ(s.spheres.size(), 1u);
	EXPECT_EQ(s.spheres[0].medium, -1);
	EXPECT_TRUE(warnedAbout(s, "ghost"));
}

TEST(FlattenTest, UnsupportedMediumTypeFallsBackToHomogeneousAndWarns) {
	// A genuinely unrecognized medium type still falls back to homogeneous
	// with a warning - the generic case cloud/rgbgrid/uniformgrid/nanovdb
	// all now opt out of. "nanovdb" used to be this test's own example
	// (pbrt-v4 supports it, this loader didn't) - it's real, recognized
	// input now, so this uses a type name pbrt-v4 has no such grammar for
	// at all instead.
	const FlatScene s = flattenSource(
		"MakeNamedMedium \"g\" \"string type\" [ \"notarealmedium\" ]\n");
	ASSERT_EQ(s.media.size(), 1u);
	EXPECT_EQ(s.media[0].type, "homogeneous");
	EXPECT_TRUE(warnedAbout(s, "notarealmedium"));
}

TEST(FlattenTest, UniformgridParsesDensityArray) {
	// nx=2,ny=1,nz=1: 2 voxels, "float density" holds exactly 2 numbers,
	// one per voxel.
	const FlatScene s = flattenSource(
		"MakeNamedMedium \"g\" \"string type\" [ \"uniformgrid\" ]\n"
		"  \"integer nx\" [ 2 ] \"integer ny\" [ 1 ] \"integer nz\" [ 1 ]\n"
		"  \"float density\" [ 0.25 0.75 ]\n");
	ASSERT_EQ(s.media.size(), 1u);
	const Medium &m = s.media[0];
	EXPECT_EQ(m.type, "uniformgrid");
	EXPECT_EQ(m.nx, 2);
	ASSERT_EQ(m.gridDensity.size(), 2u);
	EXPECT_DOUBLE_EQ(m.gridDensity[0], 0.25);
	EXPECT_DOUBLE_EQ(m.gridDensity[1], 0.75);
}

TEST(FlattenTest, UniformgridMissingDensityIsEmptyAndWarns) {
	const FlatScene s = flattenSource(
		"MakeNamedMedium \"g\" \"string type\" [ \"uniformgrid\" ]\n");
	ASSERT_EQ(s.media.size(), 1u);
	EXPECT_EQ(s.media[0].type, "uniformgrid");
	EXPECT_TRUE(s.media[0].gridDensity.empty());
	EXPECT_TRUE(warnedAbout(s, "density"));
}

TEST(FlattenTest, UniformgridWrongDensityLengthIsEmptyAndWarns) {
	// nx=2,ny=1,nz=1 expects 2 numbers; giving only 1 must not be silently
	// truncated/padded - dropped entirely, with a warning.
	const FlatScene s = flattenSource(
		"MakeNamedMedium \"g\" \"string type\" [ \"uniformgrid\" ]\n"
		"  \"integer nx\" [ 2 ] \"integer ny\" [ 1 ] \"integer nz\" [ 1 ]\n"
		"  \"float density\" [ 0.5 ]\n");
	ASSERT_EQ(s.media.size(), 1u);
	EXPECT_TRUE(s.media[0].gridDensity.empty());
	EXPECT_TRUE(warnedAbout(s, "g"));
}

// ===========================================================================
// nanovdb - MakeNamedMedium "nanovdb" (Medium::nanovdbFilename's own
// comment). File reading/densification happens in pbrt_cpu_builder.h, not
// here - these tests only pin flatten()'s own capture/validation logic.
// ===========================================================================

TEST(FlattenTest, NanoVdbCapturesFilenameAndDefaultsGridName) {
	const FlatScene s = flattenSource(
		"MakeNamedMedium \"fog\" \"string type\" [ \"nanovdb\" ] "
		"\"string filename\" [ \"clouds.nvdb\" ]\n");
	ASSERT_EQ(s.media.size(), 1u);
	const Medium &m = s.media[0];
	EXPECT_EQ(m.type, "nanovdb");
	EXPECT_EQ(m.nanovdbFilename, "clouds.nvdb");
	EXPECT_EQ(m.nanovdbGridName, "density");
}

TEST(FlattenTest, NanoVdbExplicitGridNameOverridesDefault) {
	const FlatScene s = flattenSource(
		"MakeNamedMedium \"fog\" \"string type\" [ \"nanovdb\" ] "
		"\"string filename\" [ \"clouds.nvdb\" ] "
		"\"string gridname\" [ \"smoke\" ]\n");
	ASSERT_EQ(s.media.size(), 1u);
	EXPECT_EQ(s.media[0].nanovdbGridName, "smoke");
}

TEST(FlattenTest, NanoVdbMissingFilenameWarns) {
	const FlatScene s = flattenSource(
		"MakeNamedMedium \"fog\" \"string type\" [ \"nanovdb\" ]\n");
	ASSERT_EQ(s.media.size(), 1u);
	EXPECT_EQ(s.media[0].type, "nanovdb");
	EXPECT_TRUE(s.media[0].nanovdbFilename.empty());
	EXPECT_TRUE(warnedAbout(s, "no \"string filename\""));
}

TEST(FlattenTest, NanoVdbNonzeroSigmaAWarns) {
	// grid_medium_hittable.h forces sigma_a to 0 (pure scattering) - same
	// convention/reason as uniformgrid's own identical warning.
	const FlatScene s = flattenSource(
		"MakeNamedMedium \"fog\" \"string type\" [ \"nanovdb\" ] "
		"\"string filename\" [ \"clouds.nvdb\" ] \"rgb sigma_a\" [ 0.5 0.5 0.5 ]\n");
	ASSERT_EQ(s.media.size(), 1u);
	EXPECT_TRUE(warnedAbout(s, "nanovdb medium 'fog' has a nonzero sigma_a"));
}

TEST(FlattenTest, NanoVdbTemperatureNameIsParsedNoWarning) {
	// See Medium::nanovdbTemperatureGridName's own comment - real blackbody
	// emission, no longer a "not supported" warn-and-drop.
	const FlatScene s = flattenSource(
		"MakeNamedMedium \"fire\" \"string type\" [ \"nanovdb\" ] "
		"\"string filename\" [ \"fire.nvdb\" ] "
		"\"string temperaturename\" [ \"temperature\" ]\n");
	ASSERT_EQ(s.media.size(), 1u);
	EXPECT_EQ(s.media[0].nanovdbTemperatureGridName, "temperature");
	EXPECT_FALSE(warnedAbout(s, "temperaturename"));
}

TEST(FlattenTest, NanoVdbNonzeroSigmaAWithTemperatureNameDoesNotWarn) {
	// A real sigma_a alongside "temperaturename" is exactly what makes
	// blackbody emission non-zero (see Medium::nanovdbTemperatureGridName's
	// own comment) - unlike a plain nanovdb medium (no temperature grid),
	// which still forces sigma_a to 0 and warns.
	const FlatScene s = flattenSource(
		"MakeNamedMedium \"fire\" \"string type\" [ \"nanovdb\" ] "
		"\"string filename\" [ \"fire.nvdb\" ] "
		"\"string temperaturename\" [ \"temperature\" ] "
		"\"rgb sigma_a\" [ 1 1 1 ]\n");
	ASSERT_EQ(s.media.size(), 1u);
	EXPECT_FALSE(warnedAbout(s, "sigma_a"));
}

TEST(FlattenTest, NanoVdbNonzeroSigmaAWithoutTemperatureNameStillWarns) {
	const FlatScene s = flattenSource(
		"MakeNamedMedium \"fog\" \"string type\" [ \"nanovdb\" ] "
		"\"string filename\" [ \"clouds.nvdb\" ] "
		"\"rgb sigma_a\" [ 1 1 1 ]\n");
	ASSERT_EQ(s.media.size(), 1u);
	EXPECT_TRUE(warnedAbout(s, "sigma_a"));
}

TEST(FlattenTest, NanoVdbLescaleDefaultsToOne) {
	const FlatScene s = flattenSource(
		"MakeNamedMedium \"fire\" \"string type\" [ \"nanovdb\" ] "
		"\"string filename\" [ \"fire.nvdb\" ]\n");
	ASSERT_EQ(s.media.size(), 1u);
	EXPECT_DOUBLE_EQ(s.media[0].nanovdbLeScale, 1.0);
}

TEST(FlattenTest, NanoVdbCapturesSceneCtmUnbaked) {
	// See Medium::nanovdbXform's own comment for why this is passed through
	// raw instead of pre-composed the way toMediumMat/worldMin/Max are for
	// every other grid medium type.
	const FlatScene s = flattenSource(
		"Translate 5 6 7\n"
		"MakeNamedMedium \"fog\" \"string type\" [ \"nanovdb\" ] "
		"\"string filename\" [ \"clouds.nvdb\" ]\n");
	ASSERT_EQ(s.media.size(), 1u);
	// Row-major translate components (m[3], m[7], m[11]).
	EXPECT_DOUBLE_EQ(s.media[0].nanovdbXform[3], 5.0);
	EXPECT_DOUBLE_EQ(s.media[0].nanovdbXform[7], 6.0);
	EXPECT_DOUBLE_EQ(s.media[0].nanovdbXform[11], 7.0);
}

TEST(FlattenTest, CloudMediumComputesWorldAabbAndInverseTransform) {
	// Default medium-space bounds are the unit cube; Translate+Scale places
	// it in world space. World box: (0,0,0)->(10,20,30), (1,1,1)->(12,22,32).
	const FlatScene s = flattenSource(
		"AttributeBegin\n"
		"  Translate 10 20 30\n"
		"  Scale 2 2 2\n"
		"  MakeNamedMedium \"puff\" \"string type\" [ \"cloud\" ]\n"
		"    \"rgb sigma_s\" [ 1 1 1 ] \"float density\" [ 0.5 ]\n"
		"AttributeEnd\n");
	ASSERT_EQ(s.media.size(), 1u);
	const Medium &m = s.media[0];
	EXPECT_EQ(m.type, "cloud");
	EXPECT_DOUBLE_EQ(m.p0[0], 0.0);
	EXPECT_DOUBLE_EQ(m.p1[0], 1.0);
	EXPECT_DOUBLE_EQ(m.density, 0.5);
	EXPECT_DOUBLE_EQ(m.wispiness, 1.0);
	EXPECT_DOUBLE_EQ(m.frequency, 5.0)
		<< "pbrt-v4's real default, not this codebase's own E2 showcase scene's 4.0";

	EXPECT_DOUBLE_EQ(m.worldMin[0], 10.0);
	EXPECT_DOUBLE_EQ(m.worldMin[1], 20.0);
	EXPECT_DOUBLE_EQ(m.worldMin[2], 30.0);
	EXPECT_DOUBLE_EQ(m.worldMax[0], 12.0);
	EXPECT_DOUBLE_EQ(m.worldMax[1], 22.0);
	EXPECT_DOUBLE_EQ(m.worldMax[2], 32.0);

	// world_to_medium = inverse(Translate(10,20,30)*Scale(2,2,2)):
	// mat = diag(0.5,0.5,0.5), translate = -(10,20,30)*0.5.
	EXPECT_DOUBLE_EQ(m.toMediumMat[0], 0.5);
	EXPECT_DOUBLE_EQ(m.toMediumMat[4], 0.5);
	EXPECT_DOUBLE_EQ(m.toMediumMat[8], 0.5);
	EXPECT_DOUBLE_EQ(m.toMediumTranslate[0], -5.0);
	EXPECT_DOUBLE_EQ(m.toMediumTranslate[1], -10.0);
	EXPECT_DOUBLE_EQ(m.toMediumTranslate[2], -15.0);
}

TEST(FlattenTest, CloudMediumWithNonzeroSigmaAWarns) {
	// cloud_medium_hittable/MaterialType::CloudMedium always force sigma_a
	// to 0 (pure scattering) - a scene that set a real one silently loses
	// it, which is worth a warning (see pbrt_flatten.h's own comment).
	const FlatScene s = flattenSource(
		"MakeNamedMedium \"puff\" \"string type\" [ \"cloud\" ]\n"
		"  \"rgb sigma_a\" [ 0.1 0.1 0.1 ]\n");
	ASSERT_EQ(s.media.size(), 1u);
	EXPECT_TRUE(warnedAbout(s, "sigma_a"));
}

TEST(FlattenTest, RgbGridDeinterleavesFlatTripleArray) {
	// nx=2,ny=1,nz=1: 2 voxels, "rgb sigma_s" holds 2*3=6 numbers, one RGB
	// triple per voxel (voxel 0 = (1,2,3), voxel 1 = (4,5,6)).
	const FlatScene s = flattenSource(
		"MakeNamedMedium \"nebula\" \"string type\" [ \"rgbgrid\" ]\n"
		"  \"integer nx\" [ 2 ] \"integer ny\" [ 1 ] \"integer nz\" [ 1 ]\n"
		"  \"rgb sigma_s\" [ 1 2 3  4 5 6 ]\n");
	ASSERT_EQ(s.media.size(), 1u);
	const Medium &m = s.media[0];
	EXPECT_EQ(m.type, "rgbgrid");
	ASSERT_EQ(m.sigma_s_r.size(), 2u);
	ASSERT_EQ(m.sigma_s_g.size(), 2u);
	ASSERT_EQ(m.sigma_s_b.size(), 2u);
	EXPECT_DOUBLE_EQ(m.sigma_s_r[0], 1.0);
	EXPECT_DOUBLE_EQ(m.sigma_s_g[0], 2.0);
	EXPECT_DOUBLE_EQ(m.sigma_s_b[0], 3.0);
	EXPECT_DOUBLE_EQ(m.sigma_s_r[1], 4.0);
	EXPECT_DOUBLE_EQ(m.sigma_s_g[1], 5.0);
	EXPECT_DOUBLE_EQ(m.sigma_s_b[1], 6.0);
	EXPECT_TRUE(m.sigma_a_r.empty())
		<< "sigma_a was never given, so its channel vectors stay absent";
}

TEST(FlattenTest, RgbGridWrongArrayLengthIsDroppedWithWarning) {
	// nx=2,ny=1,nz=1 expects 6 numbers; giving only 3 (one voxel) must not
	// be silently truncated/padded - it's dropped entirely, with a warning.
	const FlatScene s = flattenSource(
		"MakeNamedMedium \"nebula\" \"string type\" [ \"rgbgrid\" ]\n"
		"  \"integer nx\" [ 2 ] \"integer ny\" [ 1 ] \"integer nz\" [ 1 ]\n"
		"  \"rgb sigma_s\" [ 1 2 3 ]\n");
	ASSERT_EQ(s.media.size(), 1u);
	EXPECT_TRUE(s.media[0].sigma_s_r.empty());
	EXPECT_TRUE(warnedAbout(s, "nebula"));
}

// ===========================================================================
// Camera medium (pbrt-v4's "camera medium" - MediumInterface's own
// "outside" name active when the Camera directive is parsed)
// ===========================================================================

TEST(FlattenTest, MediumInterfaceBeforeCameraSetsTheCameraMedium) {
	const FlatScene s = flattenSource(
		"MakeNamedMedium \"fog\" \"string type\" \"homogeneous\"\n"
		"  \"rgb sigma_a\" [ 0.1 0.1 0.1 ] \"rgb sigma_s\" [ 0.2 0.2 0.2 ]\n"
		"MediumInterface \"\" \"fog\"\n"
		"Camera \"perspective\" \"float fov\" [ 40 ]\n"
		"WorldBegin\n"
		+ std::string(kQuadMesh));
	ASSERT_EQ(s.media.size(), 1u);
	EXPECT_EQ(s.cameraMediumIndex, 0);
	EXPECT_FALSE(warnedAbout(s, "camera medium"));
}

TEST(FlattenTest, NoMediumInterfaceLeavesCameraMediumUnset) {
	const FlatScene s = flattenSource(
		"Camera \"perspective\" \"float fov\" [ 40 ]\n"
		"WorldBegin\n"
		+ std::string(kQuadMesh));
	EXPECT_EQ(s.cameraMediumIndex, -1);
}

TEST(FlattenTest, InsideNameBeforeCameraDoesNotSetTheCameraMedium) {
	// Only "outside" (the camera's own side of the interface) counts - the
	// same MediumInterface directive's "inside" name is irrelevant to the
	// camera, matching real pbrt-v4's own convention.
	const FlatScene s = flattenSource(
		"MakeNamedMedium \"fog\" \"string type\" \"homogeneous\"\n"
		"MediumInterface \"fog\" \"\"\n"
		"Camera \"perspective\" \"float fov\" [ 40 ]\n"
		"WorldBegin\n"
		+ std::string(kQuadMesh));
	EXPECT_EQ(s.cameraMediumIndex, -1);
}

TEST(FlattenTest, CameraMediumCombinedWithPerShapeMediumWarnsAndIsDropped) {
	const FlatScene s = flattenSource(
		"MakeNamedMedium \"fog\" \"string type\" \"homogeneous\"\n"
		"MediumInterface \"\" \"fog\"\n"
		"Camera \"perspective\" \"float fov\" [ 40 ]\n"
		"WorldBegin\n"
		"AttributeBegin\n"
		"  MediumInterface \"fog\" \"\"\n"
		"  Shape \"sphere\" \"float radius\" [ 1 ]\n"
		"AttributeEnd\n");
	EXPECT_EQ(s.cameraMediumIndex, -1);
	EXPECT_TRUE(warnedAbout(s, "camera medium"));
	// The per-shape medium itself is unaffected by the camera-medium warning.
	ASSERT_EQ(s.spheres.size(), 1u);
	EXPECT_EQ(s.spheres[0].medium, 0);
}

// The pbrt way of bounding the camera's fog (pbrt_scenes/sunbeams-in-mist.pbrt): one full sphere around the camera with Material "interface", the camera medium inside and
// nothing outside. The fog becomes the camera medium clipped to that sphere, and the sphere an empty shell.
TEST(FlattenTest, AnInterfaceSphereAroundTheCameraBoundsTheCameraMedium) {
	const FlatScene s = flattenSource(
		"MakeNamedMedium \"fog\" \"string type\" \"homogeneous\"\n"
		"MediumInterface \"\" \"fog\"\n"
		"Camera \"perspective\" \"float fov\" [ 40 ]\n"
		"WorldBegin\n"
		"AttributeBegin\n"
		"  MediumInterface \"fog\" \"\"\n"
		"  Material \"interface\"\n"
		"  Translate 1 2 3\n"
		"  Shape \"sphere\" \"float radius\" [ 10 ]\n"
		"AttributeEnd\n"
		+ std::string(kQuadMesh));
	EXPECT_EQ(s.cameraMediumIndex, 0);
	EXPECT_FALSE(warnedAbout(s, "camera medium"));
	EXPECT_DOUBLE_EQ(s.cameraMediumBound[3], 10.0);
	EXPECT_DOUBLE_EQ(s.cameraMediumBound[1], 2.0);
	EXPECT_DOUBLE_EQ(s.cameraMediumBound[2], 3.0);
	ASSERT_EQ(s.spheres.size(), 1u);
	EXPECT_EQ(s.spheres[0].medium, -1) << "the sphere is left as an empty interface shell";
}

TEST(FlattenTest, AnInterfaceSphereThatMissesTheCameraDoesNotBoundIt) {
	// The camera (at the origin) is outside the sphere: pbrt would put the camera's rays in the fog until they cross the sphere, which is not a bounded fog; dropped as before.
	const FlatScene s = flattenSource(
		"MakeNamedMedium \"fog\" \"string type\" \"homogeneous\"\n"
		"MediumInterface \"\" \"fog\"\n"
		"Camera \"perspective\" \"float fov\" [ 40 ]\n"
		"WorldBegin\n"
		"AttributeBegin\n"
		"  MediumInterface \"fog\" \"\"\n"
		"  Material \"interface\"\n"
		"  Translate 50 0 0\n"
		"  Shape \"sphere\" \"float radius\" [ 10 ]\n"
		"AttributeEnd\n");
	EXPECT_EQ(s.cameraMediumIndex, -1);
	EXPECT_EQ(s.cameraMediumBound[3], 0.0);
	EXPECT_TRUE(warnedAbout(s, "camera medium"));
	ASSERT_EQ(s.spheres.size(), 1u);
	EXPECT_EQ(s.spheres[0].medium, 0);
}

// A fog in a layer as the camera medium (this renderer's "heightfog": height_fog.h): its top and thickness read, and kept with a cloud's medium in the same scene (it has
// thinned to nothing at the clouds' height), where a homogeneous camera medium is dropped.
TEST(FlattenTest, AHeightFogCameraMediumIsKeptWithCloudsAboveIt) {
	const FlatScene s = flattenSource(
		"MakeNamedMedium \"valley\" \"string type\" [ \"heightfog\" ] \"rgb sigma_s\" [ 0.05 0.05 0.05 ] \"float top\" [ 30 ] \"float thickness\" [ 4 ]\n"
		"MediumInterface \"\" \"valley\"\n"
		"Camera \"perspective\" \"float fov\" [ 40 ]\n"
		"WorldBegin\n"
		"MakeNamedMedium \"puff\" \"string type\" [ \"homogeneous\" ]\n"
		"AttributeBegin\n"
		"  MediumInterface \"puff\" \"\"\n"
		"  Material \"interface\"\n"
		"  Translate 0 500 0\n"
		"  Shape \"sphere\" \"float radius\" [ 100 ]\n"
		"AttributeEnd\n"
		+ std::string(kQuadMesh));
	ASSERT_EQ(s.cameraMediumIndex, 0);
	EXPECT_EQ(s.media[0].type, "heightfog");
	EXPECT_DOUBLE_EQ(s.media[0].fogTop, 30.0);
	EXPECT_DOUBLE_EQ(s.media[0].fogThickness, 4.0);
	EXPECT_FALSE(warnedAbout(s, "camera medium is ignored"));
	ASSERT_EQ(s.spheres.size(), 1u);
	EXPECT_EQ(s.spheres[0].medium, 1) << "the cloud keeps its own medium";
}

TEST(FlattenTest, NonHomogeneousCameraMediumWarnsAndIsDropped) {
	const FlatScene s = flattenSource(
		"MakeNamedMedium \"puff\" \"string type\" [ \"cloud\" ]\n"
		"MediumInterface \"\" \"puff\"\n"
		"Camera \"perspective\" \"float fov\" [ 40 ]\n"
		"WorldBegin\n"
		+ std::string(kQuadMesh));
	EXPECT_EQ(s.cameraMediumIndex, -1);
	EXPECT_TRUE(warnedAbout(s, "camera"));
	EXPECT_TRUE(warnedAbout(s, "cloud"));
}

TEST(FlattenTest, RotationAloneDoesNotCountAsNonUniformScale) {
	// A rotation leaves all three basis lengths at 1; a naive check that looked
	// at raw matrix entries rather than their lengths would warn here.
	const FlatScene s = flattenSource(
		"Rotate 37 0.3 0.5 0.8\nShape \"sphere\" \"float radius\" [ 1 ]\n");
	EXPECT_FALSE(warnedAbout(s, "ellipsoid"));
	EXPECT_NEAR(s.spheres[0].radius, 1.0, 1e-9);
}
