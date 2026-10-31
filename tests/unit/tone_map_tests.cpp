// tone_map_tests.cpp
// Unit tests for src/shared/tone_map.h
//
// Tests are grouped into three areas:
//   1. linear_to_srgb  -- sRGB OETF piecewise correctness
//   2. ACES Narkowicz  -- aces_narkowicz curve properties
//   3. apply_tone_map  -- ToneMapMode dispatch including Reinhard and None

#include <gtest/gtest.h>
#include <cmath>
#include "../../src/shared/tone_map.h"

// ---------------------------------------------------------------------------
// linear_to_srgb
// ---------------------------------------------------------------------------

TEST(LinearToSRGB, ZeroMapsToZero) {
	EXPECT_DOUBLE_EQ(linear_to_srgb(0.0), 0.0);
}

TEST(LinearToSRGB, OneMapsToOne) {
	EXPECT_DOUBLE_EQ(linear_to_srgb(1.0), 1.0);
}

TEST(LinearToSRGB, NegativeClampedToZero) {
	EXPECT_DOUBLE_EQ(linear_to_srgb(-0.5), 0.0);
}

TEST(LinearToSRGB, AboveOneClampedToOne) {
	EXPECT_DOUBLE_EQ(linear_to_srgb(2.0), 1.0);
}

TEST(LinearToSRGB, LowLinearUsesLinearSegment) {
	// For x <= 0.0031308, sRGB = 12.92 * x
	double x = 0.001;
	EXPECT_NEAR(linear_to_srgb(x), 12.92 * x, 1e-10);
}

TEST(LinearToSRGB, MidLinearUsesPowerSegment) {
	// Our implementation uses a minimax polynomial aligned with pbrt-v4's
	// enoki-derived approximation.  Verify it matches the IEC 61966-2-1
	// reference formula to within 1e-5 (the polynomial's documented error).
	for (double x : {0.01, 0.1, 0.5, 0.9}) {
		double ref = 1.055 * std::pow(x, 1.0 / 2.4) - 0.055;
		EXPECT_NEAR(linear_to_srgb(x), ref, 1e-5) << "Mismatch at x=" << x;
	}
}

TEST(LinearToSRGB, ContinuousAtKnee) {
	// The two segments must meet continuously at x = 0.0031308
	double knee = 0.0031308;
	double linear_val = 12.92 * knee;
	double power_val  = 1.055 * std::pow(knee, 1.0 / 2.4) - 0.055;
	EXPECT_NEAR(linear_val, power_val, 1e-4);
}

TEST(LinearToSRGB, IsMonotone) {
	// A sample of values must be strictly increasing
	double prev = linear_to_srgb(0.0);
	for (int i = 1; i <= 20; ++i) {
		double x = i / 20.0;
		double cur = linear_to_srgb(x);
		EXPECT_GT(cur, prev) << "Not monotone at x=" << x;
		prev = cur;
	}
}

// ---------------------------------------------------------------------------
// aces_narkowicz
// ---------------------------------------------------------------------------

TEST(ACESNarkowicz, ZeroMapsToZero) {
	EXPECT_DOUBLE_EQ(aces_narkowicz(0.0), 0.0);
}

TEST(ACESNarkowicz, NegativeClampedToZero) {
	EXPECT_DOUBLE_EQ(aces_narkowicz(-1.0), 0.0);
}

TEST(ACESNarkowicz, OutputBoundedByOne) {
	for (double x : {0.001, 0.1, 0.5, 1.0, 2.0, 5.0, 100.0}) {
		double v = aces_narkowicz(x);
		EXPECT_GE(v, 0.0) << "Negative output at x=" << x;
		EXPECT_LE(v, 1.0) << "Output exceeds 1 at x=" << x;
	}
}

TEST(ACESNarkowicz, IsMonotone) {
	double prev = 0.0;
	for (int i = 1; i <= 50; ++i) {
		double x = i * 0.1;
		double cur = aces_narkowicz(x);
		EXPECT_GE(cur, prev) << "Not monotone at x=" << x;
		prev = cur;
	}
}

TEST(ACESNarkowicz, HighExposureConvergesToOne) {
	// Very bright values should map close to 1
	EXPECT_GT(aces_narkowicz(100.0), 0.99);
}

TEST(ACESNarkowicz, MidExposureReasonable) {
	// Exposure = 1 (no scale) maps to roughly 0.83 per the Narkowicz curve
	double v = aces_narkowicz(1.0);
	EXPECT_GT(v, 0.7);
	EXPECT_LT(v, 1.0);
}

// ---------------------------------------------------------------------------
// apply_tone_map / ToneMapMode dispatch
// ---------------------------------------------------------------------------

TEST(ApplyToneMap, ACESModeMatchesHelper) {
	double x = 2.5;
	EXPECT_DOUBLE_EQ(apply_tone_map(x, ToneMapMode::ACES), aces_narkowicz(x));
}

TEST(ApplyToneMap, ReinhardModeIsXOverOnePlusX) {
	double x = 3.0;
	EXPECT_NEAR(apply_tone_map(x, ToneMapMode::Reinhard), x / (1.0 + x), 1e-12);
}

TEST(ApplyToneMap, NoneModeIsIdentityForPositive) {
	double x = 0.7;
	EXPECT_DOUBLE_EQ(apply_tone_map(x, ToneMapMode::None), x);
}

TEST(ApplyToneMap, NoneModeClampNegativeToZero) {
	EXPECT_DOUBLE_EQ(apply_tone_map(-1.0, ToneMapMode::None), 0.0);
}

TEST(ApplyToneMap, ReinhardZeroMapsToZero) {
	EXPECT_DOUBLE_EQ(apply_tone_map(0.0, ToneMapMode::Reinhard), 0.0);
}

TEST(ApplyToneMap, AllModesReturnNonNegative) {
	for (auto mode : {ToneMapMode::ACES, ToneMapMode::Reinhard, ToneMapMode::None}) {
		EXPECT_GE(apply_tone_map(0.0, mode), 0.0);
		EXPECT_GE(apply_tone_map(1.0, mode), 0.0);
		EXPECT_GE(apply_tone_map(-0.5, mode), 0.0);
	}
}

TEST(ApplyToneMap, AllModesReturnAtMostOneForSaturated) {
	for (auto mode : {ToneMapMode::ACES, ToneMapMode::Reinhard}) {
		// These operators are designed to stay <= 1 for any non-negative input
		EXPECT_LE(apply_tone_map(1000.0, mode), 1.0);
	}
}

// ---------------------------------------------------------------------------
// tone_map_mode_from_name -- --tonemap flag / launcher_args.h dispatch
// ---------------------------------------------------------------------------

TEST(ToneMapModeFromName, RecognizesAllThreeModes) {
	ToneMapMode mode;
	EXPECT_TRUE(tone_map_mode_from_name("aces", mode));
	EXPECT_EQ(mode, ToneMapMode::ACES);
	EXPECT_TRUE(tone_map_mode_from_name("reinhard", mode));
	EXPECT_EQ(mode, ToneMapMode::Reinhard);
	EXPECT_TRUE(tone_map_mode_from_name("none", mode));
	EXPECT_EQ(mode, ToneMapMode::None);
}

TEST(ToneMapModeFromName, RejectsUnrecognizedNames) {
	ToneMapMode mode = ToneMapMode::ACES;
	EXPECT_FALSE(tone_map_mode_from_name("bogus", mode));
	EXPECT_FALSE(tone_map_mode_from_name("", mode));
	EXPECT_FALSE(tone_map_mode_from_name("ACES", mode))
		<< "names are case-sensitive lowercase, matching --sampler's own convention";
}

// ---------------------------------------------------------------------------
// AgX and Khronos PBR Neutral -- whole-colour curves (apply_tone_map_rgb)
// ---------------------------------------------------------------------------

namespace {
double hueOf(double r, double g, double b) {   // degrees, 0..360
	const double mx = std::fmax(r, std::fmax(g, b)), mn = std::fmin(r, std::fmin(g, b)), d = mx - mn;
	if (d <= 0.0) return 0.0;
	double h = mx == r ? std::fmod((g - b) / d, 6.0) : mx == g ? (b - r) / d + 2.0 : (r - g) / d + 4.0;
	h *= 60.0;
	return h < 0.0 ? h + 360.0 : h;
}
}  // namespace

TEST(ToneMapModeFromName, KnowsAgxAndNeutral) {
	ToneMapMode mode;
	EXPECT_TRUE(tone_map_mode_from_name("agx", mode));
	EXPECT_EQ(mode, ToneMapMode::AgX);
	EXPECT_TRUE(tone_map_mode_from_name("neutral", mode));
	EXPECT_EQ(mode, ToneMapMode::Neutral);
	EXPECT_TRUE(tone_map_mode_from_name("agx-punchy", mode));
	EXPECT_EQ(mode, ToneMapMode::AgXPunchy);
	EXPECT_TRUE(tone_map_mode_from_name("filmic", mode));
	EXPECT_EQ(mode, ToneMapMode::Filmic);
}

// Filmic against Blender 5.2's own Filmic (base contrast), measured on a linear test image saved through its view transform (8-bit, so within 2/255 of what
// Blender shows): greys from 4 stops under middle grey to 4 over, and a pure red whose other channels come up as it passes 4 stops over grey.
TEST(ApplyToneMapRgb, FilmicMatchesBlendersFilmic) {
	const auto display = [](double r, double g, double b, int channel) {
		apply_tone_map_rgb(r, g, b, ToneMapMode::Filmic);
		return linear_to_srgb(channel == 0 ? r : channel == 1 ? g : b);
	};
	const double greys[][2] = {{0.01125, 0.1098}, {0.045, 0.2549}, {0.18, 0.502}, {0.72, 0.7569}, {2.88, 0.9255}};
	for (const auto &gv : greys) EXPECT_NEAR(display(gv[0], gv[0], gv[0], 1), gv[1], 0.008) << "grey " << gv[0];
	EXPECT_NEAR(display(3.3866, 0, 0, 0), 0.9412, 0.008);
	EXPECT_NEAR(display(3.3866, 0, 0, 1), 0.0, 0.01) << "a red 2 stops over grey keeps its saturation";
	EXPECT_NEAR(display(13.5466, 0, 0, 1), 0.0314, 0.03);
	EXPECT_NEAR(display(54.1863, 0, 0, 1), 0.5529, 0.04) << "6 stops over grey it washes towards white";
}

// The punchy look: the same curve with more contrast (a mid grey darker, a bright one still near white) and more colour.
TEST(ApplyToneMapRgb, AgxPunchyHasMoreContrastAndColourThanAgx) {
	double r = 0.18, g = 0.18, b = 0.18, pr = r, pg = g, pb = b;
	apply_tone_map_rgb(r, g, b, ToneMapMode::AgX);
	apply_tone_map_rgb(pr, pg, pb, ToneMapMode::AgXPunchy);
	EXPECT_LT(pg, g) << "mid grey darker";
	double cr = 0.6, cg = 0.25, cb = 0.1, kr = cr, kg = cg, kb = cb;
	apply_tone_map_rgb(cr, cg, cb, ToneMapMode::AgX);
	apply_tone_map_rgb(kr, kg, kb, ToneMapMode::AgXPunchy);
	EXPECT_GT((kr - kb) / (kr + 1e-9), (cr - cb) / (cr + 1e-9)) << "an orange more saturated";
}

TEST(ApplyToneMapRgb, AcesAndReinhardAreStillTheirPerChannelCurves) {
	for (ToneMapMode mode : {ToneMapMode::ACES, ToneMapMode::Reinhard, ToneMapMode::None}) {
		double r = 2.0, g = 0.5, b = 0.05;
		apply_tone_map_rgb(r, g, b, mode);
		EXPECT_DOUBLE_EQ(r, apply_tone_map(2.0, mode));
		EXPECT_DOUBLE_EQ(g, apply_tone_map(0.5, mode));
		EXPECT_DOUBLE_EQ(b, apply_tone_map(0.05, mode));
	}
}

// Every curve: black stays black, grey stays grey, the result is in [0, 1], and more light never gives a darker colour.
TEST(ApplyToneMapRgb, AgxAndNeutralKeepGreyGreyAndAreInRangeAndRising) {
	for (ToneMapMode mode : {ToneMapMode::AgX, ToneMapMode::AgXPunchy, ToneMapMode::Neutral, ToneMapMode::Filmic}) {
		double r = 0.0, g = 0.0, b = 0.0;
		apply_tone_map_rgb(r, g, b, mode);
		EXPECT_LT(g, 0.002) << tone_map_mode_display_name(mode);
		double previous = -1.0;
		for (double x = 0.001; x < 200.0; x *= 1.5) {
			double rr = x, gg = x, bb = x;
			apply_tone_map_rgb(rr, gg, bb, mode);
			EXPECT_NEAR(rr, gg, 1e-3) << "grey stays grey at " << x;
			EXPECT_NEAR(bb, gg, 1e-3) << "grey stays grey at " << x;
			EXPECT_GE(gg, 0.0);
			EXPECT_LE(gg, 1.0);
			EXPECT_GE(gg, previous - 1e-9) << "rising at " << x;
			previous = gg;
		}
		EXPECT_GT(previous, 0.9) << "very bright light is near white";
	}
}

// The reason for them: a bright saturated light. ACES on each channel turns a bright blue (a glowing crystal) cyan-ward and a bright red yellow-ward; AgX and
// PBR Neutral keep the hue much closer.
TEST(ApplyToneMapRgb, ABrightSaturatedLightKeepsItsHueUnderAgxAndNeutralBetterThanUnderAces) {
	struct Light { double r, g, b; };
	for (const Light l : {Light{0.4, 1.2, 8.0}, Light{8.0, 0.9, 0.2}}) {
		const double want = hueOf(l.r, l.g, l.b);
		const auto shift = [&](ToneMapMode mode) {
			double r = l.r, g = l.g, b = l.b;
			apply_tone_map_rgb(r, g, b, mode);
			const double d = std::fabs(hueOf(r, g, b) - want);
			return std::fmin(d, 360.0 - d);
		};
		const double aces = shift(ToneMapMode::ACES), agx = shift(ToneMapMode::AgX), neutral = shift(ToneMapMode::Neutral);
		EXPECT_LT(agx, aces) << "AgX against ACES, light " << l.r << " " << l.g << " " << l.b;
		EXPECT_LT(neutral, aces) << "Neutral against ACES";
		EXPECT_LT(neutral, 6.0) << "Neutral keeps the hue within a few degrees";
	}
}

// PBR Neutral's promise: below its knee (0.76) a colour is what it was, but for a small offset at the darkest end.
TEST(ApplyToneMapRgb, NeutralLeavesTheColoursBelowItsKneeAsTheyWere) {
	double r = 0.6, g = 0.3, b = 0.15;
	apply_tone_map_rgb(r, g, b, ToneMapMode::Neutral);
	EXPECT_NEAR(r, 0.6 - 0.04, 1e-12);
	EXPECT_NEAR(g, 0.3 - 0.04, 1e-12);
	EXPECT_NEAR(b, 0.15 - 0.04, 1e-12);
}
