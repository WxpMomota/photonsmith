// measured_material_tests.cpp
// Unit tests for the MeasuredMaterial wrapper in src/shared/materials.h.
//
// pbrt-v4 alignment reference: materials.h MeasuredMaterial (Apache-2.0)
//
// Tests:
//   1. DefaultConstruct: default-constructed MeasuredMaterial has null brdf pointer
//   2. WavelengthDefaults: default wavelengths match sRGB primaries (R~612, G~549, B~465)
//   3. ConstructFromPointer: brdf pointer and wavelengths are stored correctly
//   4. HasSubsurfaceScattering: always false (mirrors pbrt-v4)
//   5. GetBxdf_PropagatesPointer: get_bxdf() returns a MeasuredBxDF holding the same pointer
//   6. GetBxdf_PropagatesWavelengths: get_bxdf() propagates all three lambda values
//   7. GetBxdf_IgnoresContext: different contexts produce the same BxDF (no texture dependency)
//   8. NullBrdf_GetBxdfReturnsNullData: get_bxdf() on null-brdf material yields MeasuredBxDF with null data

#include <gtest/gtest.h>
#include <cmath>

#include "../../src/shared/materials.h"

// ---------------------------------------------------------------------------
// Fixture helpers
// ---------------------------------------------------------------------------

// Build a minimal (non-usable for evaluation) MeasuredBRDFData so we can
// take its address without invoking Build().  Tests here only exercise the
// material wrapper layer, not BxDF evaluation.
static MeasuredBRDFData g_dummy_data;

static MaterialEvalContext<double> make_ctx(
	double px = 0, double py = 0, double pz = 0,
	double nx = 0, double ny = 1, double nz = 0,
	double wo_x = 0, double wo_y = 0, double wo_z = 1,
	double u = 0.5, double v = 0.5)
{
	MaterialEvalContext<double> ctx;
	ctx.px = px; ctx.py = py; ctx.pz = pz;
	ctx.nx = nx; ctx.ny = ny; ctx.nz = nz;
	ctx.wo_x = wo_x; ctx.wo_y = wo_y; ctx.wo_z = wo_z;
	ctx.u = u; ctx.v = v;
	return ctx;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

TEST(MeasuredMaterialTest, DefaultConstruct) {
	MeasuredMaterial mat;
	EXPECT_EQ(mat.brdf, nullptr);
}

TEST(MeasuredMaterialTest, ConstructFromPointer) {
	MeasuredMaterial mat(&g_dummy_data);
	EXPECT_EQ(mat.brdf, &g_dummy_data);
}

TEST(MeasuredMaterialTest, HasSubsurfaceScattering) {
	// pbrt-v4 MeasuredMaterial::HasSubsurfaceScattering() is always false.
	EXPECT_FALSE(MeasuredMaterial::has_subsurface_scattering());

	MeasuredMaterial mat(&g_dummy_data);
	// Also confirm via an instance (constexpr is accessible both ways).
	EXPECT_FALSE(mat.has_subsurface_scattering());
}

TEST(MeasuredMaterialTest, GetBxdf_PropagatesPointer) {
	MeasuredMaterial mat(&g_dummy_data);
	auto ctx = make_ctx();
	MeasuredBxDF<double> bxdf = mat.get_bxdf(ctx);
	EXPECT_EQ(bxdf.data, &g_dummy_data);
}

TEST(MeasuredMaterialTest, GetBxdf_IgnoresContext) {
	// MeasuredMaterial has no textures: different contexts must yield the
	// same BxDF (same pointer).
	MeasuredMaterial mat(&g_dummy_data);

	auto ctx1 = make_ctx(0, 0, 0,  0, 1, 0,  0, 0, 1,  0.1, 0.2);
	auto ctx2 = make_ctx(5, 3, 1,  0, 0, 1,  1, 0, 0,  0.9, 0.7);

	MeasuredBxDF<double> b1 = mat.get_bxdf(ctx1);
	MeasuredBxDF<double> b2 = mat.get_bxdf(ctx2);

	EXPECT_EQ(b1.data, b2.data);
}

TEST(MeasuredMaterialTest, NullBrdf_GetBxdfReturnsNullData) {
	MeasuredMaterial mat; // brdf = nullptr
	auto ctx = make_ctx();
	MeasuredBxDF<double> bxdf = mat.get_bxdf(ctx);
	EXPECT_EQ(bxdf.data, nullptr);
}

// ---------------------------------------------------------------------------
// The spectral table as RGB (measured_rgb_quadrature.h): a quadrature over the CIE matching functions under D65, so a measured table reads the colour pbrt's film gives it
// (the three fixed wavelengths it replaced read the bundled tables' blue 16% high).
// ---------------------------------------------------------------------------
namespace {
struct FakeSpectra {
	float (*value)(float lambda);
	float Eval(float, float, float, float, float lambda) const { return value(lambda); }
};
}  // namespace

TEST(MeasuredRgbQuadrature, EachChannelsWeightsSumToOne) {
	const float lambda[RT_MEASURED_BINS] = {RT_MEASURED_LAMBDAS};
	const float wr[RT_MEASURED_BINS] = {RT_MEASURED_WR}, wg[RT_MEASURED_BINS] = {RT_MEASURED_WG}, wb[RT_MEASURED_BINS] = {RT_MEASURED_WB};
	double sr = 0, sg = 0, sb = 0;
	for (int k = 0; k < RT_MEASURED_BINS; ++k) {
		sr += wr[k]; sg += wg[k]; sb += wb[k];
		EXPECT_GT(lambda[k], 360.0f);
		EXPECT_LT(lambda[k], 830.0f);
		if (k > 0) EXPECT_GT(lambda[k], lambda[k - 1]) << "wavelengths ascend";
	}
	EXPECT_NEAR(sr, 1.0, 1e-5);
	EXPECT_NEAR(sg, 1.0, 1e-5);
	EXPECT_NEAR(sb, 1.0, 1e-5);
}

TEST(MeasuredRgbQuadrature, AFlatSpectrumReadsAsTheSameFlatColour) {
	const FakeSpectra half{[](float) { return 0.5f; }};
	float rgb[3];
	mbxdf_detail::SpectraToRgb(half, 0.3f, 0.7f, 0.1f, 0.2f, rgb);
	for (float c : rgb) EXPECT_NEAR(c, 0.5f, 1e-5f);
}

TEST(MeasuredRgbQuadrature, AReddishSpectrumReadsRedAndABluishOneBlue) {
	float rgb[3];
	mbxdf_detail::SpectraToRgb(FakeSpectra{[](float l) { return l > 590.0f ? 1.0f : 0.0f; }}, 0, 0, 0, 0, rgb);
	EXPECT_GT(rgb[0], 0.5f);
	EXPECT_LT(rgb[2], 0.1f);
	mbxdf_detail::SpectraToRgb(FakeSpectra{[](float l) { return l < 490.0f ? 1.0f : 0.0f; }}, 0, 0, 0, 0, rgb);
	EXPECT_GT(rgb[2], 0.5f);
	EXPECT_LT(rgb[0], 0.1f);
}

TEST(MeasuredRgbQuadrature, NegativeValuesAreClampedAtEveryStep) {
	float rgb[3];
	mbxdf_detail::SpectraToRgb(FakeSpectra{[](float) { return -2.0f; }}, 0, 0, 0, 0, rgb);
	for (float c : rgb) EXPECT_EQ(c, 0.0f);
}
