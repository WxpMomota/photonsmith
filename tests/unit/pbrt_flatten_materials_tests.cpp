/**
 * @file pbrt_flatten_materials_tests.cpp
 * @brief pbrt_flatten tests: Materials and emission.
 * The helpers are in pbrt_flatten_test_helpers.h; this file is one part of what used to be a single pbrt_flatten_tests.cpp.
 */

#include "pbrt_flatten_test_helpers.h"

// ===========================================================================
// Materials and emission
// ===========================================================================

TEST(FlattenMaterialTest, PbrtNamesMapStraightAcross) {
	const FlatScene s = flattenSource(
		"Material \"coateddiffuse\" \"rgb reflectance\" [ .2 .4 .6 ] \"float roughness\" [ .3 ]\n"
		+ std::string(kQuadMesh));
	ASSERT_EQ(s.materials.size(), 1u);
	EXPECT_EQ(s.materials[0].kind, MaterialKind::CoatedDiffuse);
	EXPECT_DOUBLE_EQ(s.materials[0].color[1], 0.4);
	EXPECT_DOUBLE_EQ(s.materials[0].roughness, 0.3);
}

TEST(FlattenMaterialTest, CoatedDiffuseReflectanceImagemapIsThreadedThrough) {
	// pbrt's own ganesha scene's exact binding shape - Material::
	// textureFilename's own comment on why CoatedDiffuse is gated in
	// alongside Diffuse (previously Diffuse-only, warning for every other
	// kind).
	const FlatScene s = flattenSource(
		"Texture \"tmap\" \"spectrum\" \"imagemap\" \"string filename\" [ \"statue.png\" ]\n"
		"Material \"coateddiffuse\" \"texture reflectance\" [ \"tmap\" ]\n"
		+ std::string(kQuadMesh));
	ASSERT_EQ(s.materials.size(), 1u);
	EXPECT_EQ(s.materials[0].textureFilename, "statue.png");
	EXPECT_DOUBLE_EQ(s.materials[0].textureScale, 1.0);
	EXPECT_FALSE(warnedAbout(s, "coateddiffuse"));
}

TEST(FlattenMaterialTest, CoatedDiffuseReflectanceScaleWrappedImagemapCarriesTheScale) {
	// barcelona-pavilion's own dominant binding shape.
	const FlatScene s = flattenSource(
		"Texture \"tmap\" \"spectrum\" \"imagemap\" \"string filename\" [ \"concrete.png\" ]\n"
		"Texture \"tmap-scaled\" \"spectrum\" \"scale\" \"texture tex\" [ \"tmap\" ] "
		"\"float scale\" [ 0.7 ]\n"
		"Material \"coateddiffuse\" \"texture reflectance\" [ \"tmap-scaled\" ]\n"
		+ std::string(kQuadMesh));
	ASSERT_EQ(s.materials.size(), 1u);
	EXPECT_EQ(s.materials[0].textureFilename, "concrete.png");
	EXPECT_DOUBLE_EQ(s.materials[0].textureScale, 0.7);
}

TEST(FlattenMaterialTest, DiffuseReflectanceScaleWrappedImagemapCarriesTheScale) {
	// barcelona-pavilion's own dominant binding shape, now also supported for
	// plain "diffuse" surfaces, not just "coateddiffuse" - see
	// Material::textureScale's own comment.
	const FlatScene s = flattenSource(
		"Texture \"tmap\" \"spectrum\" \"imagemap\" \"string filename\" [ \"concrete.png\" ]\n"
		"Texture \"tmap-scaled\" \"spectrum\" \"scale\" \"texture tex\" [ \"tmap\" ] "
		"\"float scale\" [ 0.7 ]\n"
		"Material \"diffuse\" \"texture reflectance\" [ \"tmap-scaled\" ]\n"
		+ std::string(kQuadMesh));
	ASSERT_EQ(s.materials.size(), 1u);
	EXPECT_EQ(s.materials[0].textureFilename, "concrete.png");
	EXPECT_DOUBLE_EQ(s.materials[0].textureScale, 0.7);
	EXPECT_FALSE(warnedAbout(s, "diffuse"));
}

TEST(FlattenMaterialTest, ReflectanceImagemapDefaultsToSRGBGammaRepeatWrapNoInvert) {
	// No "encoding"/"wrap"/"invert" params at all - Material::textureGamma/
	// textureWrap/textureInvert's own comments on why these particular
	// defaults (2.2, "repeat", false) were chosen.
	const FlatScene s = flattenSource(
		"Texture \"tmap\" \"spectrum\" \"imagemap\" \"string filename\" [ \"statue.png\" ]\n"
		"Material \"diffuse\" \"texture reflectance\" [ \"tmap\" ]\n"
		+ std::string(kQuadMesh));
	ASSERT_EQ(s.materials.size(), 1u);
	EXPECT_DOUBLE_EQ(s.materials[0].textureGamma, 2.2);
	EXPECT_EQ(s.materials[0].textureWrap, "repeat");
	EXPECT_FALSE(s.materials[0].textureInvert);
}

TEST(FlattenMaterialTest, ReflectanceImagemapLinearEncodingResolvesToGammaOne) {
	const FlatScene s = flattenSource(
		"Texture \"tmap\" \"spectrum\" \"imagemap\" \"string filename\" [ \"rough.png\" ] "
		"\"string encoding\" [ \"linear\" ]\n"
		"Material \"diffuse\" \"texture reflectance\" [ \"tmap\" ]\n"
		+ std::string(kQuadMesh));
	ASSERT_EQ(s.materials.size(), 1u);
	EXPECT_DOUBLE_EQ(s.materials[0].textureGamma, 1.0);
}

TEST(FlattenMaterialTest, ReflectanceImagemapExplicitGammaEncodingResolvesToThatValue) {
	const FlatScene s = flattenSource(
		"Texture \"tmap\" \"spectrum\" \"imagemap\" \"string filename\" [ \"scan.png\" ] "
		"\"string encoding\" [ \"gamma 1.8\" ]\n"
		"Material \"diffuse\" \"texture reflectance\" [ \"tmap\" ]\n"
		+ std::string(kQuadMesh));
	ASSERT_EQ(s.materials.size(), 1u);
	EXPECT_DOUBLE_EQ(s.materials[0].textureGamma, 1.8);
}

TEST(FlattenMaterialTest, ReflectanceImagemapExplicitWrapAndInvertAreThreadedThrough) {
	const FlatScene s = flattenSource(
		"Texture \"tmap\" \"spectrum\" \"imagemap\" \"string filename\" [ \"tile.png\" ] "
		"\"string wrap\" [ \"black\" ] \"bool invert\" [ true ]\n"
		"Material \"diffuse\" \"texture reflectance\" [ \"tmap\" ]\n"
		+ std::string(kQuadMesh));
	ASSERT_EQ(s.materials.size(), 1u);
	EXPECT_EQ(s.materials[0].textureWrap, "black");
	EXPECT_TRUE(s.materials[0].textureInvert);
}

TEST(FlattenMaterialTest, ReflectanceImagemapNonFiniteGammaEncodingFallsBackAndWarns) {
	// "gamma nan"/"gamma inf" parse successfully via std::stod's own
	// strtod semantics (no exception) - resolveTextureGamma must reject
	// these explicitly rather than letting a NaN/Inf gamma reach
	// stbi_ldr_to_hdr_gamma() and corrupt every texel with no diagnostic.
	const FlatScene s = flattenSource(
		"Texture \"tmap\" \"spectrum\" \"imagemap\" \"string filename\" [ \"x.png\" ] "
		"\"string encoding\" [ \"gamma nan\" ]\n"
		"Material \"diffuse\" \"texture reflectance\" [ \"tmap\" ]\n"
		+ std::string(kQuadMesh));
	ASSERT_EQ(s.materials.size(), 1u);
	EXPECT_DOUBLE_EQ(s.materials[0].textureGamma, 2.2);
	EXPECT_TRUE(warnedAbout(s, "encoding"));
}

TEST(FlattenMaterialTest, ReflectanceImagemapNegativeGammaEncodingFallsBackAndWarns) {
	const FlatScene s = flattenSource(
		"Texture \"tmap\" \"spectrum\" \"imagemap\" \"string filename\" [ \"x.png\" ] "
		"\"string encoding\" [ \"gamma -1.0\" ]\n"
		"Material \"diffuse\" \"texture reflectance\" [ \"tmap\" ]\n"
		+ std::string(kQuadMesh));
	ASSERT_EQ(s.materials.size(), 1u);
	EXPECT_DOUBLE_EQ(s.materials[0].textureGamma, 2.2);
	EXPECT_TRUE(warnedAbout(s, "encoding"));
}

TEST(FlattenMaterialTest, ReflectanceImagemapUnrecognizedEncodingFallsBackAndWarns) {
	const FlatScene s = flattenSource(
		"Texture \"tmap\" \"spectrum\" \"imagemap\" \"string filename\" [ \"x.png\" ] "
		"\"string encoding\" [ \"srgb8.icc\" ]\n"
		"Material \"diffuse\" \"texture reflectance\" [ \"tmap\" ]\n"
		+ std::string(kQuadMesh));
	ASSERT_EQ(s.materials.size(), 1u);
	EXPECT_DOUBLE_EQ(s.materials[0].textureGamma, 2.2);
	EXPECT_TRUE(warnedAbout(s, "encoding"));
}

TEST(FlattenMaterialTest, ReflectanceImagemapUnrecognizedWrapFallsBackToRepeatAndWarns) {
	const FlatScene s = flattenSource(
		"Texture \"tmap\" \"spectrum\" \"imagemap\" \"string filename\" [ \"x.png\" ] "
		"\"string wrap\" [ \"Clamp\" ]\n"  // wrong case - not the recognized "clamp"
		"Material \"diffuse\" \"texture reflectance\" [ \"tmap\" ]\n"
		+ std::string(kQuadMesh));
	ASSERT_EQ(s.materials.size(), 1u);
	EXPECT_EQ(s.materials[0].textureWrap, "repeat");
	EXPECT_TRUE(warnedAbout(s, "wrap"));
}

// "string filter": point / bilinear (pbrt's default) / trilinear / ewa, case-insensitive; an unknown name warns and gives bilinear.
TEST(FlattenMaterialTest, ReflectanceImagemapFilterIsThreadedThroughAndDefaultsToBilinear) {
	const auto filterOf = [](const char *filterParam) {
		const FlatScene s = flattenSource(
			std::string("Texture \"tmap\" \"spectrum\" \"imagemap\" \"string filename\" [ \"x.png\" ] ") + filterParam + "\n"
			"Material \"diffuse\" \"texture reflectance\" [ \"tmap\" ]\n" + std::string(kQuadMesh));
		EXPECT_EQ(s.materials.size(), 1u);
		return s.materials.empty() ? -1 : s.materials[0].textureFilterIndex;
	};
	EXPECT_EQ(filterOf(""), 1);
	EXPECT_EQ(filterOf("\"string filter\" [ \"point\" ]"), 0);
	EXPECT_EQ(filterOf("\"string filter\" [ \"bilinear\" ]"), 1);
	EXPECT_EQ(filterOf("\"string filter\" [ \"trilinear\" ]"), 2);
	EXPECT_EQ(filterOf("\"string filter\" [ \"EWA\" ]"), 3);
	const FlatScene bad = flattenSource(
		"Texture \"tmap\" \"spectrum\" \"imagemap\" \"string filename\" [ \"x.png\" ] \"string filter\" [ \"cubic\" ]\n"
		"Material \"diffuse\" \"texture reflectance\" [ \"tmap\" ]\n" + std::string(kQuadMesh));
	ASSERT_EQ(bad.materials.size(), 1u);
	EXPECT_EQ(bad.materials[0].textureFilterIndex, 1);
	EXPECT_TRUE(warnedAbout(bad, "filter"));
}

TEST(FlattenMaterialTest, RoughnessImagemapEncodingIsNowResolvedNotIgnored) {
	// Superseded by DielectricRoughnessEncodingWrapInvertAreNowResolved
	// (below in this file) once roughness gained real encoding/wrap/invert
	// support - roughness is no longer one of the "ignored, warns" slots.
	const FlatScene s = flattenSource(
		"Texture \"rmap\" \"float\" \"imagemap\" \"string filename\" [ \"rough.png\" ] "
		"\"string encoding\" [ \"linear\" ]\n"
		"Material \"dielectric\" \"texture roughness\" [ \"rmap\" ]\n"
		+ std::string(kQuadMesh));
	ASSERT_EQ(s.materials.size(), 1u);
	EXPECT_EQ(s.materials[0].roughnessTextureFilename, "rough.png");
	EXPECT_DOUBLE_EQ(s.materials[0].roughnessTextureOptions.gamma, 1.0);
	EXPECT_FALSE(warnedAbout(s, "\"encoding\"/\"wrap\"/\"invert\""));
}

TEST(FlattenMaterialTest, CoatedDiffuseReflectanceCheckerboardResolvesToAProceduralTexture) {
	// checkerboard/fbm/marble/mix used to be Diffuse-only (a documented
	// scope cut); now also resolved for CoatedDiffuse, same as the "scale"
	// unwrap and bare-imagemap cases already were - see
	// hasCheckerReflectance's own comment.
	const FlatScene s = flattenSource(
		"Texture \"chk\" \"spectrum\" \"checkerboard\"\n"
		"Material \"coateddiffuse\" \"texture reflectance\" [ \"chk\" ]\n"
		+ std::string(kQuadMesh));
	ASSERT_EQ(s.materials.size(), 1u);
	EXPECT_TRUE(s.materials[0].hasCheckerReflectance);
	EXPECT_FALSE(warnedAbout(s, "coateddiffuse"));
}

TEST(FlattenMaterialTest, CoatedDiffuseReflectanceFbmResolvesToAProceduralTexture) {
	// Same broadening as checkerboard above, for fbm specifically - the fbm
	// gate at MaterialKind::Diffuse|CoatedDiffuse was previously untested for
	// CoatedDiffuse at this level.
	const FlatScene s = flattenSource(
		"Texture \"cloud\" \"float\" \"fbm\" \"integer octaves\" [ 4 ]\n"
		"Material \"coateddiffuse\" \"texture reflectance\" [ \"cloud\" ]\n"
		+ std::string(kQuadMesh));
	ASSERT_EQ(s.materials.size(), 1u);
	EXPECT_TRUE(s.materials[0].hasFbmReflectance);
	EXPECT_EQ(s.materials[0].fbmOctaves, 4);
	EXPECT_FALSE(warnedAbout(s, "coateddiffuse"));
}

TEST(FlattenMaterialTest, CoatedDiffuseReflectanceMarbleResolvesToAProceduralTexture) {
	// Same broadening as checkerboard above, for marble specifically - the
	// marble gate at MaterialKind::Diffuse|CoatedDiffuse was previously
	// untested for CoatedDiffuse at this level.
	const FlatScene s = flattenSource(
		"Texture \"stone\" \"spectrum\" \"marble\"\n"
		"Material \"coateddiffuse\" \"texture reflectance\" [ \"stone\" ]\n"
		+ std::string(kQuadMesh));
	ASSERT_EQ(s.materials.size(), 1u);
	EXPECT_TRUE(s.materials[0].hasMarbleReflectance);
	EXPECT_FALSE(warnedAbout(s, "coateddiffuse"));
}

TEST(FlattenMaterialTest, CheckerboardTex1NestsAnotherCheckerboardResolvesRecursively) {
	const FlatScene s = flattenSource(
		"Texture \"inner\" \"spectrum\" \"checkerboard\" \"rgb tex1\" [ 1 0 0 ] "
		"\"rgb tex2\" [ 0 1 0 ] \"float uscale\" [ 3 ] \"float vscale\" [ 4 ]\n"
		"Texture \"outer\" \"spectrum\" \"checkerboard\" \"texture tex1\" [ \"inner\" ]\n"
		"Material \"diffuse\" \"texture reflectance\" [ \"outer\" ]\n"
		+ std::string(kQuadMesh));
	ASSERT_EQ(s.materials.size(), 1u);
	EXPECT_TRUE(s.materials[0].hasCheckerReflectance);
	EXPECT_EQ(s.materials[0].checkerTex1Nested.kind, "checkerboard");
	EXPECT_DOUBLE_EQ(s.materials[0].checkerTex1Nested.color1[0], 1.0);
	EXPECT_DOUBLE_EQ(s.materials[0].checkerTex1Nested.color2[1], 1.0);
	EXPECT_DOUBLE_EQ(s.materials[0].checkerTex1Nested.uscale, 3.0);
	EXPECT_DOUBLE_EQ(s.materials[0].checkerTex1Nested.vscale, 4.0);
	// GPU-approximation fallback: checkerColor1 gets the nested pattern's
	// own average colour (red+green)/2, not left at its unrelated default.
	EXPECT_DOUBLE_EQ(s.materials[0].checkerColor1[0], 0.5);
	EXPECT_DOUBLE_EQ(s.materials[0].checkerColor1[1], 0.5);
	EXPECT_FALSE(warnedAbout(s, "not supported"));
}

TEST(FlattenMaterialTest, MixTex1NestsAnotherMixResolvesRecursively) {
	const FlatScene s = flattenSource(
		"Texture \"inner\" \"spectrum\" \"mix\" \"rgb tex1\" [ 1 0 0 ] "
		"\"rgb tex2\" [ 0 1 0 ] \"float amount\" [ 0.25 ]\n"
		"Texture \"outer\" \"spectrum\" \"mix\" \"texture tex1\" [ \"inner\" ]\n"
		"Material \"diffuse\" \"texture reflectance\" [ \"outer\" ]\n"
		+ std::string(kQuadMesh));
	ASSERT_EQ(s.materials.size(), 1u);
	EXPECT_TRUE(s.materials[0].hasMixReflectance);
	EXPECT_EQ(s.materials[0].mixTex1Nested.kind, "mix");
	EXPECT_DOUBLE_EQ(s.materials[0].mixTex1Nested.color1[0], 1.0);
	EXPECT_DOUBLE_EQ(s.materials[0].mixTex1Nested.color2[1], 1.0);
	EXPECT_DOUBLE_EQ(s.materials[0].mixTex1Nested.amount, 0.25);
	EXPECT_FALSE(warnedAbout(s, "not supported"));
	// GPU-approximation fallback: a nested "mix" must weight by its own
	// amount (0.25 -> 75% red, 25% green), not naively average 50/50 the
	// way checkerboard's own nesting fallback correctly does (its two
	// cells really do cover equal area) - see
	// nestedProceduralAverageColor()'s own comment.
	EXPECT_NEAR(s.materials[0].mixColor1[0], 0.75, 1e-9);
	EXPECT_NEAR(s.materials[0].mixColor1[1], 0.25, 1e-9);
}

TEST(FlattenMaterialTest, CheckerboardTex1NestingAThirdLevelStillFallsThroughToGenericWarning) {
	// The second level's own tex1/tex2 must be a flat literal or a bare
	// imagemap - NOT a further nested procedural (a third level) - see
	// NestedProceduralTexture's own comment on the deliberate two-level cap.
	const FlatScene s = flattenSource(
		"Texture \"innermost\" \"spectrum\" \"checkerboard\"\n"
		"Texture \"inner\" \"spectrum\" \"checkerboard\" \"texture tex1\" [ \"innermost\" ]\n"
		"Texture \"outer\" \"spectrum\" \"checkerboard\" \"texture tex1\" [ \"inner\" ]\n"
		"Material \"diffuse\" \"texture reflectance\" [ \"outer\" ]\n"
		+ std::string(kQuadMesh));
	ASSERT_EQ(s.materials.size(), 1u);
	EXPECT_FALSE(s.materials[0].hasCheckerReflectance);
	EXPECT_TRUE(warnedAbout(s, "not supported"));
}

TEST(FlattenMaterialTest, CheckerboardTex1NestingAnUnrelatedProceduralStillFallsThroughToGenericWarning) {
	// tex1 names a real Texture, but one that's neither a bare imagemap nor
	// checkerboard/mix (fbm has no tex1/tex2 concept at all) - still an
	// unresolvable nested reference, same as any other unsupported case.
	const FlatScene s = flattenSource(
		"Texture \"cloud\" \"float\" \"fbm\"\n"
		"Texture \"outer\" \"spectrum\" \"checkerboard\" \"texture tex1\" [ \"cloud\" ]\n"
		"Material \"diffuse\" \"texture reflectance\" [ \"outer\" ]\n"
		+ std::string(kQuadMesh));
	ASSERT_EQ(s.materials.size(), 1u);
	EXPECT_FALSE(s.materials[0].hasCheckerReflectance);
	EXPECT_TRUE(warnedAbout(s, "not supported"));
}

TEST(FlattenMaterialTest, DiffuseReflectanceWindyResolvesToAProceduralTexture) {
	// pbrt-v4 WindyTexture is parameterless - just the "is this bound" flag.
	const FlatScene s = flattenSource(
		"Texture \"grass\" \"float\" \"windy\"\n"
		"Material \"diffuse\" \"texture reflectance\" [ \"grass\" ]\n"
		+ std::string(kQuadMesh));
	ASSERT_EQ(s.materials.size(), 1u);
	EXPECT_TRUE(s.materials[0].hasWindyReflectance);
	EXPECT_FALSE(warnedAbout(s, "diffuse"));
}

TEST(FlattenMaterialTest, CoatedDiffuseReflectanceWrinkledResolvesToAProceduralTexture) {
	const FlatScene s = flattenSource(
		"Texture \"paper\" \"float\" \"wrinkled\" \"integer octaves\" [ 5 ] \"float roughness\" [ 0.4 ]\n"
		"Material \"coateddiffuse\" \"texture reflectance\" [ \"paper\" ]\n"
		+ std::string(kQuadMesh));
	ASSERT_EQ(s.materials.size(), 1u);
	EXPECT_TRUE(s.materials[0].hasWrinkledReflectance);
	EXPECT_EQ(s.materials[0].wrinkledOctaves, 5);
	EXPECT_DOUBLE_EQ(s.materials[0].wrinkledRoughness, 0.4);
	EXPECT_FALSE(warnedAbout(s, "coateddiffuse"));
}

TEST(FlattenMaterialTest, DiffuseReflectanceDotsResolvesToAProceduralTexture) {
	const FlatScene s = flattenSource(
		"Texture \"polka\" \"spectrum\" \"dots\" \"rgb inside\" [ 1 0 0 ] \"rgb outside\" [ 0 0 1 ]\n"
		"Material \"diffuse\" \"texture reflectance\" [ \"polka\" ]\n"
		+ std::string(kQuadMesh));
	ASSERT_EQ(s.materials.size(), 1u);
	EXPECT_TRUE(s.materials[0].hasDotsReflectance);
	// pbrt-v4 swaps the two (its constructor takes (outside, inside), Create() passes (inside, outside)): the file's "outside" colours the dots, its "inside" the ground.
	EXPECT_DOUBLE_EQ(s.materials[0].dotsInsideColor[2], 1.0) << "the dots are the \"outside\" colour";
	EXPECT_DOUBLE_EQ(s.materials[0].dotsOutsideColor[0], 1.0) << "the ground is the \"inside\" colour";
	EXPECT_FALSE(warnedAbout(s, "diffuse"));
}

TEST(FlattenMaterialTest, DotsDefaultsMatchPbrtV4InsideWhiteOutsideBlack) {
	const FlatScene s = flattenSource(
		"Texture \"polka\" \"spectrum\" \"dots\"\n"
		"Material \"diffuse\" \"texture reflectance\" [ \"polka\" ]\n"
		+ std::string(kQuadMesh));
	ASSERT_EQ(s.materials.size(), 1u);
	EXPECT_TRUE(s.materials[0].hasDotsReflectance);
	// "inside" defaults to white and "outside" to black, and pbrt-v4 uses them the other way round: black dots on a white ground.
	EXPECT_DOUBLE_EQ(s.materials[0].dotsInsideColor[0], 0.0);
	EXPECT_DOUBLE_EQ(s.materials[0].dotsOutsideColor[0], 1.0);
}

TEST(FlattenMaterialTest, ADotsTextureCarriesItsOwnUvMapping) {
	const FlatScene s = flattenSource(
		"Texture \"polka\" \"spectrum\" \"dots\" \"float uscale\" [ 6 ] \"float vscale\" [ 3 ] \"float udelta\" [ 0.25 ]\n"
		"Material \"diffuse\" \"texture reflectance\" [ \"polka\" ]\n"
		+ std::string(kQuadMesh));
	ASSERT_EQ(s.materials.size(), 1u);
	EXPECT_DOUBLE_EQ(s.materials[0].dotsMapping.su, 6.0);
	EXPECT_DOUBLE_EQ(s.materials[0].dotsMapping.sv, 3.0);
	EXPECT_DOUBLE_EQ(s.materials[0].dotsMapping.du, 0.25);
	EXPECT_DOUBLE_EQ(s.materials[0].dotsMapping.dv, 0.0);
}

TEST(FlattenMaterialTest, CoatedDiffuseReflectanceBilerpResolvesToAProceduralTexture) {
	const FlatScene s = flattenSource(
		"Texture \"grad\" \"spectrum\" \"bilerp\" \"rgb v00\" [ 1 0 0 ] \"rgb v11\" [ 0 0 1 ]\n"
		"Material \"coateddiffuse\" \"texture reflectance\" [ \"grad\" ]\n"
		+ std::string(kQuadMesh));
	ASSERT_EQ(s.materials.size(), 1u);
	EXPECT_TRUE(s.materials[0].hasBilerpReflectance);
	EXPECT_DOUBLE_EQ(s.materials[0].bilerpV00[0], 1.0);
	EXPECT_DOUBLE_EQ(s.materials[0].bilerpV11[2], 1.0);
	// v01/v10 default to pbrt-v4's own white/black respectively.
	EXPECT_DOUBLE_EQ(s.materials[0].bilerpV01[0], 1.0);
	EXPECT_DOUBLE_EQ(s.materials[0].bilerpV10[0], 0.0);
	EXPECT_FALSE(warnedAbout(s, "coateddiffuse"));
}

TEST(FlattenMaterialTest, MixAmountImagemapCarriesTheTextureFilename) {
	// pbrt-v4's real "amount" bound to its own Texture (e.g. an fbm-driven
	// dirt/wear mask) - previously always fell through to the generic "not
	// supported" warning; now resolved the same one-level-nested-bare-
	// imagemap way as tex1/tex2 - see Material::mixAmountTextureFilename's
	// own comment.
	const FlatScene s = flattenSource(
		"Texture \"wear\" \"float\" \"imagemap\" \"string filename\" [ \"wear.png\" ]\n"
		"Texture \"dirt\" \"spectrum\" \"mix\" \"rgb tex1\" [ 1 0 0 ] \"rgb tex2\" [ 0 0 1 ] "
		"\"texture amount\" [ \"wear\" ]\n"
		"Material \"diffuse\" \"texture reflectance\" [ \"dirt\" ]\n"
		+ std::string(kQuadMesh));
	ASSERT_EQ(s.materials.size(), 1u);
	EXPECT_TRUE(s.materials[0].hasMixReflectance);
	EXPECT_EQ(s.materials[0].mixAmountTextureFilename, "wear.png");
	EXPECT_FALSE(warnedAbout(s, "diffuse"));
}

TEST(FlattenMaterialTest, MixAmountBoundToACheckerboardResolvesAtTheSecondLevel) {
	// "amount" naming a Texture that is itself NOT a bare imagemap (here, a
	// checkerboard - a second procedural texture, not a colour sample) now
	// resolves at the second level, the SAME up-to-two-level nesting tex1/
	// tex2 already had (Material::mixAmountNested's own comment) - no
	// longer a warn-and-fall-back case.
	const FlatScene s = flattenSource(
		"Texture \"chk\" \"spectrum\" \"checkerboard\"\n"
		"Texture \"dirt\" \"spectrum\" \"mix\" \"rgb tex1\" [ 1 0 0 ] \"rgb tex2\" [ 0 0 1 ] "
		"\"texture amount\" [ \"chk\" ]\n"
		"Material \"diffuse\" \"texture reflectance\" [ \"dirt\" ]\n"
		+ std::string(kQuadMesh));
	ASSERT_EQ(s.materials.size(), 1u);
	EXPECT_TRUE(s.materials[0].hasMixReflectance);
	EXPECT_TRUE(s.materials[0].mixAmountTextureFilename.empty());
	EXPECT_EQ(s.materials[0].mixAmountNested.kind, "checkerboard");
	EXPECT_FALSE(warnedAbout(s, "diffuse"));
}

TEST(FlattenMaterialTest, MixAmountBoundToAThirdLevelProceduralStillWarns) {
	// A THIRD level (amount -> checkerboard -> mix) is still out of scope -
	// NestedProceduralTexture's own tex1/tex2/amount slots are capped at a
	// bare literal/imagemap, never a further nested procedural (see that
	// struct's own comment) - confirms the cap still applies to "amount"
	// even though it now reaches the second level tex1/tex2 already did.
	const FlatScene s = flattenSource(
		"Texture \"inner\" \"spectrum\" \"mix\" \"rgb tex1\" [ 1 1 1 ] \"rgb tex2\" [ 0 0 0 ]\n"
		"Texture \"chk\" \"spectrum\" \"checkerboard\" \"texture tex1\" [ \"inner\" ]\n"
		"Texture \"dirt\" \"spectrum\" \"mix\" \"rgb tex1\" [ 1 0 0 ] \"rgb tex2\" [ 0 0 1 ] "
		"\"texture amount\" [ \"chk\" ]\n"
		"Material \"diffuse\" \"texture reflectance\" [ \"dirt\" ]\n"
		+ std::string(kQuadMesh));
	ASSERT_EQ(s.materials.size(), 1u);
	EXPECT_FALSE(s.materials[0].hasMixReflectance);
	EXPECT_TRUE(warnedAbout(s, "diffuse"));
}

TEST(FlattenMaterialTest, DiffuseTransmissionReflectanceCheckerboardStillWarns) {
	// Regression guard for the Diffuse->CoatedDiffuse procedural-texture
	// broadening above: DiffuseTransmission must stay excluded (no bundled
	// scene binds a checkerboard/fbm/marble/mix to a diffusetransmission
	// reflectance), so this must still warn rather than the broadened OR
	// condition accidentally picking it up too.
	const FlatScene s = flattenSource(
		"Texture \"chk\" \"spectrum\" \"checkerboard\"\n"
		"Material \"diffusetransmission\" \"texture reflectance\" [ \"chk\" ]\n"
		+ std::string(kQuadMesh));
	ASSERT_EQ(s.materials.size(), 1u);
	EXPECT_FALSE(s.materials[0].hasCheckerReflectance);
	EXPECT_TRUE(warnedAbout(s, "diffusetransmission"));
}

TEST(FlattenMaterialTest, DiffuseReflectanceScaleWrappingACheckerboardStillWarns) {
	// A regression guard for the reflectance scale-unwrap added alongside
	// CoatedDiffuse support: unwrapping "scale" must only ever feed the
	// "imagemap" branch, never silently reveal a wrapped checkerboard/fbm/
	// marble/mix underneath and drop its own scale factor unnoticed - see
	// the unwrap's own "imgTex, not tex" comment in the flatten loop.
	const FlatScene s = flattenSource(
		"Texture \"chk\" \"spectrum\" \"checkerboard\"\n"
		"Texture \"chk-scaled\" \"spectrum\" \"scale\" \"texture tex\" [ \"chk\" ] "
		"\"float scale\" [ 0.5 ]\n"
		"Material \"diffuse\" \"texture reflectance\" [ \"chk-scaled\" ]\n"
		+ std::string(kQuadMesh));
	ASSERT_EQ(s.materials.size(), 1u);
	EXPECT_FALSE(s.materials[0].hasCheckerReflectance);
	EXPECT_TRUE(warnedAbout(s, "diffuse"));
}

TEST(FlattenMaterialTest, DielectricEtaIsReadAsIor) {
	const FlatScene s = flattenSource(
		"Material \"dielectric\" \"float eta\" [ 1.33 ]\n" + std::string(kQuadMesh));
	ASSERT_EQ(s.materials.size(), 1u);
	EXPECT_EQ(s.materials[0].kind, MaterialKind::Dielectric);
	EXPECT_DOUBLE_EQ(s.materials[0].ior, 1.33);
}

TEST(FlattenMaterialTest, DielectricNamedGlassSpectrumResolvesToKnownIor) {
	// "spectrum eta" "glass-BK7" previously fell straight through to the
	// generic 1.5 default with NO warning at all (getFloat() only inspects
	// numbers, and a named-spectrum Param has none) - now resolves to the
	// real N-BK7 catalog value.
	const FlatScene s = flattenSource(
		"Material \"dielectric\" \"spectrum eta\" [ \"glass-BK7\" ]\n" + std::string(kQuadMesh));
	ASSERT_EQ(s.materials.size(), 1u);
	EXPECT_NEAR(s.materials[0].ior, 1.5168, 1e-4);
	EXPECT_FALSE(warnedAbout(s, "eta"));
}

TEST(FlattenMaterialTest, ThinDielectricAndCoatedDiffuseAlsoResolveNamedGlass) {
	const FlatScene sThin = flattenSource(
		"Material \"thindielectric\" \"spectrum eta\" [ \"glass-SF11\" ]\n" + std::string(kQuadMesh));
	ASSERT_EQ(sThin.materials.size(), 1u);
	// pbrt-v4's real registered name drops the "S" from Schott's SF-series
	// ("glass-F11", not "glass-SF11") - this is a genuinely unrecognized
	// name and should warn + fall back, exercising the "not a recognized
	// preset" branch rather than a typo in this test.
	EXPECT_DOUBLE_EQ(sThin.materials[0].ior, 1.5);
	EXPECT_TRUE(warnedAbout(sThin, "unrecognized glass"));

	const FlatScene sCoated = flattenSource(
		"Material \"coateddiffuse\" \"spectrum eta\" [ \"glass-F11\" ]\n" + std::string(kQuadMesh));
	ASSERT_EQ(sCoated.materials.size(), 1u);
	EXPECT_NEAR(sCoated.materials[0].ior, 1.7847, 1e-4);
}

TEST(FlattenMaterialTest, DielectricUnrecognizedNamedSpectrumWarnsAndFallsBack) {
	const FlatScene s = flattenSource(
		"Material \"dielectric\" \"spectrum eta\" [ \"not-a-real-spectrum\" ]\n" + std::string(kQuadMesh));
	ASSERT_EQ(s.materials.size(), 1u);
	EXPECT_DOUBLE_EQ(s.materials[0].ior, 1.5);
	EXPECT_TRUE(warnedAbout(s, "not a recognized glass preset"));
}

// Review-fix regression tests (commit following 8884178's review).

TEST(FlattenMaterialTest, DielectricInlineNumericSpectrumEtaWarnsInsteadOfMisreadingAWavelength) {
	// "spectrum eta" [ 400 1.5168 700 1.5142 ] is real pbrt-v4 syntax for
	// inline piecewise-linear spectral data, not a named preset - the
	// generic getFloat() read would otherwise silently read the first
	// number (400, a WAVELENGTH) as if it were the IOR itself.
	const FlatScene s = flattenSource(
		"Material \"dielectric\" \"spectrum eta\" [ 400 1.5168 700 1.5142 ]\n" + std::string(kQuadMesh));
	ASSERT_EQ(s.materials.size(), 1u);
	EXPECT_DOUBLE_EQ(s.materials[0].ior, 1.5);
	EXPECT_TRUE(warnedAbout(s, "inline wavelength/value data"));
}

TEST(FlattenMaterialTest, DielectricTextureEtaIsNotMisreadAsAnUnrecognizedSpectrum) {
	// A "texture eta" binding also stores its payload in Param::strings,
	// same as a named-spectrum "spectrum eta" - gating on Param::type
	// (checked by the glass-resolution code) rather than "is there a
	// string at all" must NOT fire the glass-preset warning for this case.
	// This scene's texture isn't "constant", so it still hits the generic,
	// pre-existing texture-not-supported warning - just not a SECOND,
	// factually wrong "unrecognized glass" one alongside it.
	const FlatScene s = flattenSource(
		"Texture \"etatex\" \"spectrum\" \"checkerboard\" \"float tex1\" [ 1.3 ] \"float tex2\" [ 1.6 ]\n"
		"Material \"dielectric\" \"texture eta\" [ \"etatex\" ]\n" + std::string(kQuadMesh));
	ASSERT_EQ(s.materials.size(), 1u);
	EXPECT_DOUBLE_EQ(s.materials[0].ior, 1.5);
	EXPECT_FALSE(warnedAbout(s, "unrecognized glass"));
	EXPECT_FALSE(warnedAbout(s, "not a recognized glass preset"));
}

TEST(FlattenMaterialTest, DielectricConstantTextureEtaResolvesWithoutASpuriousGlassWarning) {
	// A CONSTANT texture-bound eta gets rewritten in place to a real number
	// by the pre-pass at the top of this loop (Param::type becomes "rgb",
	// not "spectrum") - the glass-resolution code must not treat the
	// leftover texture-name string as an "unrecognized glass" and must not
	// clobber the correctly-resolved value.
	const FlatScene s = flattenSource(
		"Texture \"etatex\" \"spectrum\" \"constant\" \"float value\" [ 1.6 ]\n"
		"Material \"dielectric\" \"texture eta\" [ \"etatex\" ]\n" + std::string(kQuadMesh));
	ASSERT_EQ(s.materials.size(), 1u);
	EXPECT_DOUBLE_EQ(s.materials[0].ior, 1.6);
	EXPECT_FALSE(warnedAbout(s, "unrecognized glass"));
	EXPECT_FALSE(warnedAbout(s, "not a recognized glass preset"));
}

TEST(FlattenMaterialTest, SubsurfaceNamedGlassSpectrumWarnsAndUsesSubsurfaceDefault) {
	// Real pbrt-v4 SubsurfaceMaterial reads eta as a plain float only - a
	// "spectrum eta" here should fall back to subsurface's OWN default
	// (1.33), not the generic 1.5, and should warn rather than silently
	// landing on a default that doesn't match either the request or the
	// material kind's real behavior.
	const FlatScene s = flattenSource(
		"Material \"subsurface\" \"spectrum eta\" [ \"glass-BK7\" ]\n" + std::string(kQuadMesh));
	ASSERT_EQ(s.materials.size(), 1u);
	EXPECT_DOUBLE_EQ(s.materials[0].ior, 1.33);
	EXPECT_TRUE(warnedAbout(s, "subsurface"));
}

TEST(FlattenMaterialTest, SubsurfaceReflectanceAndMfpAreInvertedToCoefficients) {
	// reflectance + mfp (pbrt-v4's fourth way to give a subsurface material):
	// sigma_s + sigma_a = 1/mfp (a length), and the albedo between them is the
	// one whose effective albedo is the reflectance. The table is pbrt-v4's
	// (bssrdf_tests MatchesPbrtV4Reference pins it to pbrt's own numbers): rho_eff 0.8,
	// mfp 0.4, eta 1.33 -> sigma_a 0.008546, sigma_s 2.4915.
	const FlatScene s = flattenSource(
		"Material \"subsurface\" \"rgb reflectance\" [ 0.8 0.8 0.8 ] \"rgb mfp\" [ 0.4 0.4 0.4 ]\n" +
		std::string(kQuadMesh));
	ASSERT_EQ(s.materials.size(), 1u);
	EXPECT_NEAR(s.materials[0].sigma_a[0], 0.0085459, 1e-4);
	EXPECT_NEAR(s.materials[0].sigma_s[0], 2.491454, 1e-3);
	EXPECT_FALSE(warnedAbout(s, "reflectance"));

	// "scale" multiplies the mean free path, so the coefficients shrink by it.
	const FlatScene t = flattenSource(
		"Material \"subsurface\" \"rgb reflectance\" [ 0.8 0.8 0.8 ] \"rgb mfp\" [ 0.4 0.4 0.4 ] \"float scale\" 2\n" +
		std::string(kQuadMesh));
	ASSERT_EQ(t.materials.size(), 1u);
	EXPECT_NEAR(t.materials[0].sigma_s[0], s.materials[0].sigma_s[0] / 2, 1e-3);
}

TEST(FlattenMaterialTest, HairNamedGlassSpectrumWarnsAndUsesHairDefault) {
	const FlatScene s = flattenSource(
		"Material \"hair\" \"spectrum eta\" [ \"glass-BK7\" ]\n" + std::string(kQuadMesh));
	ASSERT_EQ(s.materials.size(), 1u);
	EXPECT_DOUBLE_EQ(s.materials[0].ior, 1.55);
	EXPECT_TRUE(warnedAbout(s, "hair"));
}

TEST(FlattenMaterialTest, InterfaceMaterialNoneMapsToInterfaceKind) {
	// pbrt-v4's real interface-material idiom (a shape bounding a
	// participating medium with no BSDF response of its own) - previously
	// fell to MaterialKind::Unsupported (opaque flat Lambertian, plus a
	// warning). Built as a real, dedicated pass-through material (CPU's
	// interface_material) - m.ior is irrelevant to it and no longer forced.
	const FlatScene s = flattenSource(
		"Material \"none\"\n" + std::string(kQuadMesh));
	ASSERT_EQ(s.materials.size(), 1u);
	EXPECT_EQ(s.materials[0].kind, MaterialKind::Interface);
	EXPECT_FALSE(warnedAbout(s, "none"));
}

TEST(FlattenMaterialTest, InterfaceMaterialCanonicalNameMapsToInterfaceKind) {
	// pbrt-v4 itself rewrites "none" and "" to "interface" (parser.cpp), so
	// `Material "interface"` is the canonical spelling real scenes use. It used
	// to fall to MaterialKind::Unsupported - an opaque flat grey Lambertian
	// plus a warning - so a medium boundary written the pbrt-v4 way rendered as
	// a solid sphere instead of disappearing.
	const FlatScene s = flattenSource(
		"Material \"interface\"\n" + std::string(kQuadMesh));
	ASSERT_EQ(s.materials.size(), 1u);
	EXPECT_EQ(s.materials[0].kind, MaterialKind::Interface);
	EXPECT_FALSE(warnedAbout(s, "interface"));
}

TEST(FlattenMaterialTest, InterfaceMaterialEmptyStringAlsoMapsToInterface) {
	// pbrt-v4 also accepts a bare empty type string for the same idiom.
	const FlatScene s = flattenSource(
		"Material \"\"\n" + std::string(kQuadMesh));
	ASSERT_EQ(s.materials.size(), 1u);
	EXPECT_EQ(s.materials[0].kind, MaterialKind::Interface);
}

TEST(FlattenMaterialTest, ConstantTextureRgbValueResolvesReflectance) {
	// A real scene author's indirection for a colour reused across several
	// materials - previously fell through to the generic "texture not
	// supported" warning, and worse, to the WRONG fallback colour (the
	// pre-loop getVec3() read the raw texture-typed "reflectance" param,
	// which has no numbers, silently returning the generic default).
	const FlatScene s = flattenSource(
		"Texture \"c\" \"spectrum\" \"constant\" \"rgb value\" [ .2 .4 .6 ]\n"
		"Material \"diffuse\" \"texture reflectance\" [ \"c\" ]\n"
		+ std::string(kQuadMesh));
	ASSERT_EQ(s.materials.size(), 1u);
	EXPECT_DOUBLE_EQ(s.materials[0].color[0], 0.2);
	EXPECT_DOUBLE_EQ(s.materials[0].color[1], 0.4);
	EXPECT_DOUBLE_EQ(s.materials[0].color[2], 0.6);
	EXPECT_FALSE(warnedAbout(s, "diffuse"));
}

TEST(FlattenMaterialTest, ConstantTextureFloatValueBroadcastsToReflectance) {
	// A float-typed constant texture feeding an RGB-consuming parameter -
	// broadcasts to RGB, matching pbrt-v4's own convention. Deliberately NOT
	// 0.5: Material::color's own in-class default is {0.5,0.5,0.5}, so that
	// value would pass this test even if the broadcast silently failed and
	// getVec3() fell back to the default - which is exactly what happened
	// before this was fixed (the rewrite wrote a 1-element numbers vector,
	// getVec3() requires >=3 and returned its default unnoticed).
	const FlatScene s = flattenSource(
		"Texture \"c\" \"float\" \"constant\" \"float value\" [ .2 ]\n"
		"Material \"diffuse\" \"texture reflectance\" [ \"c\" ]\n"
		+ std::string(kQuadMesh));
	ASSERT_EQ(s.materials.size(), 1u);
	EXPECT_DOUBLE_EQ(s.materials[0].color[0], 0.2);
	EXPECT_DOUBLE_EQ(s.materials[0].color[1], 0.2);
	EXPECT_DOUBLE_EQ(s.materials[0].color[2], 0.2);
}

TEST(FlattenMaterialTest, ConstantTextureBoundToKResolvesOnConductor) {
	// Deliberately not gated on material kind - conductor's "k" needs the
	// same resolution as diffuse-family "reflectance".
	const FlatScene s = flattenSource(
		"Texture \"c\" \"spectrum\" \"constant\" \"rgb value\" [ .9 .8 .1 ]\n"
		"Material \"conductor\" \"texture k\" [ \"c\" ]\n"
		+ std::string(kQuadMesh));
	ASSERT_EQ(s.materials.size(), 1u);
	EXPECT_DOUBLE_EQ(s.materials[0].color[0], 0.9);
	EXPECT_DOUBLE_EQ(s.materials[0].color[1], 0.8);
	EXPECT_DOUBLE_EQ(s.materials[0].color[2], 0.1);
	EXPECT_FALSE(warnedAbout(s, "conductor"));
}

TEST(FlattenMaterialTest, ConstantTextureReflectanceWinsOverConstantTextureK) {
	// Both bound to constant textures - the pre-existing "reflectance wins
	// over k" precedence (materials.pbrt's own rule) must still hold
	// regardless of which param happens to be declared later in the file.
	// This is the general Param-rewrite pre-pass's whole point: reflectance/
	// k precedence is decided ONCE, by the same code whether the values
	// came from plain numbers or a resolved constant texture - not
	// re-decided by declaration order in a separate texture-handling branch.
	const FlatScene s = flattenSource(
		"Texture \"refl\" \"spectrum\" \"constant\" \"rgb value\" [ .9 .9 .9 ]\n"
		"Texture \"kk\" \"spectrum\" \"constant\" \"rgb value\" [ 3.9 2.4 1.7 ]\n"
		"Material \"conductor\" \"texture reflectance\" [ \"refl\" ] \"texture k\" [ \"kk\" ]\n"
		+ std::string(kQuadMesh));
	ASSERT_EQ(s.materials.size(), 1u);
	EXPECT_DOUBLE_EQ(s.materials[0].color[0], 0.9);
	EXPECT_DOUBLE_EQ(s.materials[0].color[1], 0.9);
	EXPECT_DOUBLE_EQ(s.materials[0].color[2], 0.9);
}

TEST(FlattenMaterialTest, ConstantTextureEtaAndKResolveRealConductorModel) {
	// Both eta and k texture-bound with real (>1) copper-like values - the
	// rewrite pre-pass must make the explicit-RGB conductor path (which
	// needs BOTH eta and k as real numbers to activate, see the "eta"/"k"
	// resolution block just below the reflectance/k precedence) trigger
	// correctly, instead of falling to the generic metal(albedo) path with
	// an un-clamped, energy-gaining (>1) albedo.
	const FlatScene s = flattenSource(
		"Texture \"cueta\" \"spectrum\" \"constant\" \"rgb value\" [ 0.2 0.92 1.1 ]\n"
		"Texture \"cuk\" \"spectrum\" \"constant\" \"rgb value\" [ 3.9 2.45 2.14 ]\n"
		"Material \"conductor\" \"texture eta\" [ \"cueta\" ] \"texture k\" [ \"cuk\" ]\n"
		+ std::string(kQuadMesh));
	ASSERT_EQ(s.materials.size(), 1u);
	EXPECT_TRUE(s.materials[0].hasConductorPreset);
	EXPECT_DOUBLE_EQ(s.materials[0].conductorEta[0], 0.2);
	EXPECT_DOUBLE_EQ(s.materials[0].conductorK[0], 3.9);
}

TEST(FlattenMaterialTest, RoughnessFallsBackToUOrVRoughnessWhenIsotropicIsAbsent) {
	// A scene that only gives the anisotropic pair has no "roughness" key to
	// find, and without a fallback chain that silently reads as 0 - a
	// perfect mirror - regardless of what uroughness/vroughness said.
	const FlatScene s = flattenSource(
		"Material \"coateddiffuse\" \"float uroughness\" [ .25 ]\n"
		+ std::string(kQuadMesh));
	ASSERT_EQ(s.materials.size(), 1u);
	EXPECT_DOUBLE_EQ(s.materials[0].roughness, 0.25);
}

TEST(FlattenMaterialTest, VRoughnessIsUsedWhenNeitherRoughnessNorURoughnessIsGiven) {
	const FlatScene s = flattenSource(
		"Material \"coateddiffuse\" \"float vroughness\" [ .4 ]\n"
		+ std::string(kQuadMesh));
	ASSERT_EQ(s.materials.size(), 1u);
	EXPECT_DOUBLE_EQ(s.materials[0].roughness, 0.4);
}

TEST(FlattenMaterialTest, PlainRoughnessTakesPriorityOverTheAnisotropicPair) {
	const FlatScene s = flattenSource(
		"Material \"coateddiffuse\" \"float roughness\" [ .1 ] "
		"\"float uroughness\" [ .9 ]\n"
		+ std::string(kQuadMesh));
	ASSERT_EQ(s.materials.size(), 1u);
	EXPECT_DOUBLE_EQ(s.materials[0].roughness, 0.1);
}

TEST(FlattenMaterialTest, DiffuseTransmissionReadsItsOwnTransmittanceColour) {
	// Distinct from "reflectance" on purpose - a diffusetransmission material
	// that let one silently stand in for the other would look identical from
	// both sides, which is not what the scene asked for.
	const FlatScene s = flattenSource(
		"Material \"diffusetransmission\" \"rgb reflectance\" [ .8 .1 .1 ] "
		"\"rgb transmittance\" [ .1 .1 .8 ]\n"
		+ std::string(kQuadMesh));
	ASSERT_EQ(s.materials.size(), 1u);
	EXPECT_EQ(s.materials[0].kind, MaterialKind::DiffuseTransmission);
	EXPECT_DOUBLE_EQ(s.materials[0].color[0], 0.8);
	EXPECT_DOUBLE_EQ(s.materials[0].transmittance[2], 0.8);
}

TEST(FlattenMaterialTest, DiffuseTransmissionDefaultsMatchPbrt) {
	// pbrt-v4's own default for this material is 0.25 for both channels, not
	// the generic 0.5 mid-grey every other material defaults its colour to.
	const FlatScene s = flattenSource(
		"Material \"diffusetransmission\"\n" + std::string(kQuadMesh));
	ASSERT_EQ(s.materials.size(), 1u);
	EXPECT_DOUBLE_EQ(s.materials[0].transmittance[0], 0.25);
	EXPECT_DOUBLE_EQ(s.materials[0].transmittance[1], 0.25);
	EXPECT_DOUBLE_EQ(s.materials[0].transmittance[2], 0.25);
}

TEST(FlattenMaterialTest, DiffuseTransmissionReflectanceAndTransmittanceImagemapsAreThreadedThrough) {
	// barcelona-pavilion's foliage binding shape - both params bound to the
	// SAME bare imagemap - see Material::textureFilename/
	// transmittanceTextureFilename's own comments.
	const FlatScene s = flattenSource(
		"Texture \"leaf\" \"spectrum\" \"imagemap\" \"string filename\" [ \"leaf.png\" ]\n"
		"Material \"diffusetransmission\" \"texture reflectance\" [ \"leaf\" ] "
		"\"texture transmittance\" [ \"leaf\" ]\n"
		+ std::string(kQuadMesh));
	ASSERT_EQ(s.materials.size(), 1u);
	EXPECT_EQ(s.materials[0].textureFilename, "leaf.png");
	EXPECT_EQ(s.materials[0].transmittanceTextureFilename, "leaf.png");
	EXPECT_FALSE(warnedAbout(s, "diffusetransmission"));
}

TEST(FlattenMaterialTest, DiffuseTransmissionReflectanceAndTransmittanceUseDistinctTextures) {
	// Regression guard: reflectance and transmittance must resolve to their
	// OWN respective texture filename, not accidentally alias each other's.
	const FlatScene s = flattenSource(
		"Texture \"leaf-r\" \"spectrum\" \"imagemap\" \"string filename\" [ \"leaf-r.png\" ]\n"
		"Texture \"leaf-t\" \"spectrum\" \"imagemap\" \"string filename\" [ \"leaf-t.png\" ]\n"
		"Material \"diffusetransmission\" \"texture reflectance\" [ \"leaf-r\" ] "
		"\"texture transmittance\" [ \"leaf-t\" ]\n"
		+ std::string(kQuadMesh));
	ASSERT_EQ(s.materials.size(), 1u);
	EXPECT_EQ(s.materials[0].textureFilename, "leaf-r.png");
	EXPECT_EQ(s.materials[0].transmittanceTextureFilename, "leaf-t.png");
}

TEST(FlattenMaterialTest, DiffuseTransmissionReflectanceScaleWrappedImagemapCarriesTheScale) {
	// "scale"-unwrap used to be CoatedDiffuse-only; now also resolved for
	// DiffuseTransmission's own reflectance - see Material::textureScale's
	// own comment.
	const FlatScene s = flattenSource(
		"Texture \"leaf\" \"spectrum\" \"imagemap\" \"string filename\" [ \"leaf.png\" ]\n"
		"Texture \"leaf-scaled\" \"spectrum\" \"scale\" \"texture tex\" [ \"leaf\" ] "
		"\"float scale\" [ 0.5 ]\n"
		"Material \"diffusetransmission\" \"texture reflectance\" [ \"leaf-scaled\" ]\n"
		+ std::string(kQuadMesh));
	ASSERT_EQ(s.materials.size(), 1u);
	EXPECT_EQ(s.materials[0].textureFilename, "leaf.png");
	EXPECT_DOUBLE_EQ(s.materials[0].textureScale, 0.5);
	EXPECT_FALSE(warnedAbout(s, "diffusetransmission"));
}

TEST(FlattenMaterialTest, DiffuseTransmissionTransmittanceScaleWrappedImagemapCarriesItsOwnScale) {
	// Same broadening, for transmittance specifically - and with a
	// DIFFERENT scale value than reflectance's own, to confirm the two are
	// resolved independently (Material::transmittanceTextureScale's own
	// comment) rather than accidentally sharing one scale.
	const FlatScene s = flattenSource(
		"Texture \"leaf-r\" \"spectrum\" \"imagemap\" \"string filename\" [ \"leaf-r.png\" ]\n"
		"Texture \"leaf-r-scaled\" \"spectrum\" \"scale\" \"texture tex\" [ \"leaf-r\" ] "
		"\"float scale\" [ 0.5 ]\n"
		"Texture \"leaf-t\" \"spectrum\" \"imagemap\" \"string filename\" [ \"leaf-t.png\" ]\n"
		"Texture \"leaf-t-scaled\" \"spectrum\" \"scale\" \"texture tex\" [ \"leaf-t\" ] "
		"\"float scale\" [ 0.8 ]\n"
		"Material \"diffusetransmission\" \"texture reflectance\" [ \"leaf-r-scaled\" ] "
		"\"texture transmittance\" [ \"leaf-t-scaled\" ]\n"
		+ std::string(kQuadMesh));
	ASSERT_EQ(s.materials.size(), 1u);
	EXPECT_EQ(s.materials[0].textureFilename, "leaf-r.png");
	EXPECT_DOUBLE_EQ(s.materials[0].textureScale, 0.5);
	EXPECT_EQ(s.materials[0].transmittanceTextureFilename, "leaf-t.png");
	EXPECT_DOUBLE_EQ(s.materials[0].transmittanceTextureScale, 0.8);
	EXPECT_FALSE(warnedAbout(s, "diffusetransmission"));
}

TEST(FlattenMaterialTest, DielectricRoughnessImagemapIsThreadedThrough) {
	// pbrt-v4 "texture roughness" on a Dielectric bound to a bare
	// imagemap - see Material::roughnessTextureFilename's own comment.
	// Bare imagemap only, same scope as DiffuseTransmission's own
	// transmittance texture-binding above.
	const FlatScene s = flattenSource(
		"Texture \"scratch\" \"float\" \"imagemap\" \"string filename\" [ \"scratch.png\" ]\n"
		"Material \"dielectric\" \"texture roughness\" [ \"scratch\" ]\n"
		+ std::string(kQuadMesh));
	ASSERT_EQ(s.materials.size(), 1u);
	EXPECT_EQ(s.materials[0].roughnessTextureFilename, "scratch.png");
	EXPECT_FALSE(warnedAbout(s, "dielectric"));
}

TEST(FlattenMaterialTest, DielectricTransmissionFilterIsReadFromRgbTf) {
	// Non-standard "rgb tf": the OBJ/.mtl Tf transmission filter, for the
	// environment scenes migrated from .mtl assets (pbrt_scenes/environment-*.pbrt).
	const FlatScene tinted = flattenSource(
		"Material \"dielectric\" \"float eta\" [ 1.5 ] \"rgb tf\" [ 0.25 0.5 0.75 ]\n"
		+ std::string(kQuadMesh));
	ASSERT_EQ(tinted.materials.size(), 1u);
	EXPECT_DOUBLE_EQ(tinted.materials[0].transmissionFilter[0], 0.25);
	EXPECT_DOUBLE_EQ(tinted.materials[0].transmissionFilter[1], 0.5);
	EXPECT_DOUBLE_EQ(tinted.materials[0].transmissionFilter[2], 0.75);

	// Absent means white (a no-op filter), and only a dielectric reads it.
	const FlatScene plain = flattenSource(
		"Material \"dielectric\"\n" + std::string(kQuadMesh));
	ASSERT_EQ(plain.materials.size(), 1u);
	for (int c = 0; c < 3; ++c) EXPECT_DOUBLE_EQ(plain.materials[0].transmissionFilter[c], 1.0);
	const FlatScene diffuse = flattenSource(
		"Material \"diffuse\" \"rgb tf\" [ 0.1 0.1 0.1 ]\n" + std::string(kQuadMesh));
	ASSERT_EQ(diffuse.materials.size(), 1u);
	for (int c = 0; c < 3; ++c) EXPECT_DOUBLE_EQ(diffuse.materials[0].transmissionFilter[c], 1.0);
	EXPECT_FALSE(warnedAbout(tinted, "rgb tf"));
}

TEST(FlattenMaterialTest, DielectricTransmissionFilterOnRoughOrDispersiveGlassWarns) {
	const FlatScene rough = flattenSource(
		"Material \"dielectric\" \"float roughness\" [ 0.2 ] \"rgb tf\" [ 0.5 0.5 0.5 ]\n"
		+ std::string(kQuadMesh));
	EXPECT_TRUE(warnedAbout(rough, "rgb tf"));
	const FlatScene dispersive = flattenSource(
		"Material \"dielectric\" \"float abbenumber\" [ 40 ] \"rgb tf\" [ 0.5 0.5 0.5 ]\n"
		+ std::string(kQuadMesh));
	EXPECT_TRUE(warnedAbout(dispersive, "rgb tf"));
	// A white filter on rough glass is a no-op, so nothing to warn about.
	const FlatScene white = flattenSource(
		"Material \"dielectric\" \"float roughness\" [ 0.2 ]\n" + std::string(kQuadMesh));
	EXPECT_FALSE(warnedAbout(white, "rgb tf"));
}

TEST(FlattenMaterialTest, DielectricRoughnessEncodingWrapInvertAreNowResolved) {
	// Previously silently dropped for every non-primary slot - now resolved
	// the same way the primary reflectance slot's own textureGamma/
	// textureWrap/textureInvert are, via roughnessTextureOptions.
	const FlatScene s = flattenSource(
		"Texture \"scratch\" \"float\" \"imagemap\" \"string filename\" [ \"scratch.png\" ] "
		"\"string encoding\" [ \"linear\" ] \"string wrap\" [ \"black\" ] \"bool invert\" [ true ]\n"
		"Material \"dielectric\" \"texture roughness\" [ \"scratch\" ]\n"
		+ std::string(kQuadMesh));
	ASSERT_EQ(s.materials.size(), 1u);
	EXPECT_DOUBLE_EQ(s.materials[0].roughnessTextureOptions.gamma, 1.0);
	EXPECT_EQ(s.materials[0].roughnessTextureOptions.wrap, "black");
	EXPECT_EQ(s.materials[0].roughnessTextureOptions.wrapIndex, 2);
	EXPECT_TRUE(s.materials[0].roughnessTextureOptions.invert);
	EXPECT_FALSE(warnedAbout(s, "\"encoding\"/\"wrap\"/\"invert\""));
}

TEST(FlattenMaterialTest, DielectricRoughnessNoEncodingGivenKeepsThePreExistingDefault) {
	// A scene that doesn't opt in gets exactly the same default as before
	// this feature existed (gamma 2.2/"repeat"/no-invert) - no behavior
	// change for the common case.
	const FlatScene s = flattenSource(
		"Texture \"scratch\" \"float\" \"imagemap\" \"string filename\" [ \"scratch.png\" ]\n"
		"Material \"dielectric\" \"texture roughness\" [ \"scratch\" ]\n"
		+ std::string(kQuadMesh));
	ASSERT_EQ(s.materials.size(), 1u);
	EXPECT_DOUBLE_EQ(s.materials[0].roughnessTextureOptions.gamma, 2.2);
	EXPECT_EQ(s.materials[0].roughnessTextureOptions.wrap, "repeat");
	EXPECT_FALSE(s.materials[0].roughnessTextureOptions.invert);
}

TEST(FlattenMaterialTest, DiffuseTransmissionTransmittanceEncodingWrapInvertAreNowResolved) {
	const FlatScene s = flattenSource(
		"Texture \"leaf\" \"spectrum\" \"imagemap\" \"string filename\" [ \"leaf.png\" ] "
		"\"string encoding\" [ \"gamma 1.8\" ] \"string wrap\" [ \"clamp\" ] \"bool invert\" [ true ]\n"
		"Material \"diffusetransmission\" \"texture transmittance\" [ \"leaf\" ]\n"
		+ std::string(kQuadMesh));
	ASSERT_EQ(s.materials.size(), 1u);
	EXPECT_DOUBLE_EQ(s.materials[0].transmittanceTextureOptions.gamma, 1.8);
	EXPECT_EQ(s.materials[0].transmittanceTextureOptions.wrap, "clamp");
	EXPECT_EQ(s.materials[0].transmittanceTextureOptions.wrapIndex, 0);
	EXPECT_TRUE(s.materials[0].transmittanceTextureOptions.invert);
}

TEST(FlattenMaterialTest, RoughnessTextureUnrecognizedWrapWarnsAndFallsBackToRepeat) {
	const FlatScene s = flattenSource(
		"Texture \"scratch\" \"float\" \"imagemap\" \"string filename\" [ \"scratch.png\" ] "
		"\"string wrap\" [ \"Clamp\" ]\n"
		"Material \"dielectric\" \"texture roughness\" [ \"scratch\" ]\n"
		+ std::string(kQuadMesh));
	ASSERT_EQ(s.materials.size(), 1u);
	EXPECT_EQ(s.materials[0].roughnessTextureOptions.wrap, "repeat");
	EXPECT_TRUE(warnedAbout(s, "string wrap"));
}

TEST(FlattenMaterialTest, AlphaAndDisplacementStillWarnEncodingWrapInvertIsIgnored) {
	// The 2 remaining slots (alpha/displacement) are a deliberate, still-open
	// scope cut - see warnIfImagemapOptionsIgnored()'s own comment for why.
	const FlatScene s = flattenSource(
		"Texture \"disp\" \"float\" \"imagemap\" \"string filename\" [ \"disp.png\" ] "
		"\"bool invert\" [ true ]\n"
		"Material \"diffuse\" \"texture displacement\" [ \"disp\" ]\n"
		+ std::string(kQuadMesh));
	ASSERT_EQ(s.materials.size(), 1u);
	EXPECT_EQ(s.materials[0].displacementTextureFilename, "disp.png");
	EXPECT_TRUE(warnedAbout(s, "\"encoding\"/\"wrap\"/\"invert\""));
}

TEST(FlattenMaterialTest, DielectricRoughnessConstantTextureResolvesToFlatNumber) {
	// The generic constant-texture rewrite pre-pass (flatten()'s own
	// comment, runs before any per-kind logic) is kind-agnostic - a
	// "texture roughness" bound to a "constant" Texture already resolves
	// to a real flat number here with NO dedicated Dielectric-roughness
	// code needed, unlike the imagemap case above which needed real
	// wiring. Regression guard confirming that pre-pass really does cover
	// this param on this material kind.
	const FlatScene s = flattenSource(
		"Texture \"fixedRough\" \"float\" \"constant\" \"float value\" [ 0.3 ]\n"
		"Material \"dielectric\" \"texture roughness\" [ \"fixedRough\" ]\n"
		+ std::string(kQuadMesh));
	ASSERT_EQ(s.materials.size(), 1u);
	EXPECT_DOUBLE_EQ(s.materials[0].roughness, 0.3);
	EXPECT_TRUE(s.materials[0].roughnessTextureFilename.empty());
	EXPECT_FALSE(warnedAbout(s, "dielectric"));
}

TEST(FlattenMaterialTest, DielectricRoughnessScaleWrappedImagemapStillWarns) {
	// Deliberate scope cut: no "scale"-wrap support for Dielectric's own
	// roughness texture-binding (unlike Diffuse/CoatedDiffuse/
	// DiffuseTransmission's own reflectance/transmittance, which all got
	// this above).
	const FlatScene s = flattenSource(
		"Texture \"scratch\" \"float\" \"imagemap\" \"string filename\" [ \"scratch.png\" ]\n"
		"Texture \"scratch-scaled\" \"float\" \"scale\" \"texture tex\" [ \"scratch\" ] "
		"\"float scale\" [ 0.5 ]\n"
		"Material \"dielectric\" \"texture roughness\" [ \"scratch-scaled\" ]\n"
		+ std::string(kQuadMesh));
	ASSERT_EQ(s.materials.size(), 1u);
	EXPECT_TRUE(s.materials[0].roughnessTextureFilename.empty());
	EXPECT_TRUE(warnedAbout(s, "dielectric"));
}

TEST(FlattenMaterialTest, TrianglemeshWithMediumInterfaceWarnsMediumIsDropped) {
	// Triangle has no `medium` field (unlike Sphere/Disk/Cylinder), so a
	// mesh-bounded medium boundary - the most common real pbrt-v4 authoring
	// pattern for one - silently loses its medium. This must warn instead of
	// going quiet, especially now that Material "none" itself no longer
	// warns (MaterialKind::Interface, not Unsupported).
	const FlatScene s = flattenSource(
		"MakeNamedMedium \"fog\" \"string type\" \"homogeneous\"\n"
		"AttributeBegin\n"
		"  Material \"none\"\n"
		"  MediumInterface \"fog\" \"\"\n"
		"  Shape \"trianglemesh\" \"integer indices\" [ 0 1 2  0 2 3 ]\n"
		"    \"point3 P\" [ -1 -1 0   1 -1 0   1 1 0   -1 1 0 ]\n"
		"AttributeEnd\n");
	EXPECT_TRUE(warnedAbout(s, "MediumInterface"));
}

TEST(FlattenMaterialTest, SphereMediumInterfaceDoesNotWarn) {
	// Regression guard: the new trianglemesh warning must not fire for the
	// shape kinds that DO support an attached medium.
	const FlatScene s = flattenSource(
		"MakeNamedMedium \"fog\" \"string type\" \"homogeneous\"\n"
		"AttributeBegin\n"
		"  Material \"none\"\n"
		"  MediumInterface \"fog\" \"\"\n"
		"  Shape \"sphere\"\n"
		"AttributeEnd\n");
	EXPECT_FALSE(warnedAbout(s, "MediumInterface"));
}

TEST(FlattenMaterialTest, UnsupportedMaterialIsFlaggedNotSilentlySubstituted) {
	// A material rendered as diffuse looks plausible and is wrong, so the
	// substitution has to be announced. "subsurface" then "hair" used to be
	// the example here, in turn, until each became a real, CPU/GPU-supported
	// MaterialKind - every real pbrt-v4 material name now maps to a genuine
	// MaterialKind, so this uses a made-up, never-real type string instead
	// (matching materialKindFor()'s own "anything not recognized" fallback,
	// the actual case this Unsupported path exists for now - a typo'd or
	// genuinely nonexistent material name, not a real pbrt-v4 kind this
	// loader hasn't gotten to yet).
	const FlatScene s = flattenSource(
		"Material \"holographic\"\n" + std::string(kQuadMesh));
	ASSERT_EQ(s.materials.size(), 1u);
	EXPECT_EQ(s.materials[0].kind, MaterialKind::Unsupported);
	EXPECT_EQ(s.materials[0].pbrtType, "holographic");
	EXPECT_TRUE(warnedAbout(s, "holographic"));
}

TEST(FlattenMaterialTest, AreaLightRadianceAndScaleAreExtracted) {
	const FlatScene s = flattenSource(
		"AttributeBegin\n"
		"  AreaLightSource \"diffuse\" \"rgb L\" [ 17 12 4 ] \"float scale\" [ 2 ]\n"
		+ std::string(kQuadMesh) + "AttributeEnd\n");
	ASSERT_EQ(s.areaLights.size(), 1u);
	EXPECT_DOUBLE_EQ(s.areaLights[0].L[0], 17.0);
	EXPECT_DOUBLE_EQ(s.areaLights[0].L[2], 4.0);
	EXPECT_DOUBLE_EQ(s.areaLights[0].scale, 2.0);
}

TEST(FlattenMaterialTest, BlackbodyEmissionIsConvertedToARealColour) {
	// Previously warned-and-discarded the temperature entirely (getVec3
	// requires >=3 numbers, a "blackbody L" param has exactly 1, so it
	// silently fell back to flat white regardless of temperature) - now
	// converted via resolveEmissionColor() to a real RGB colour. Verified
	// against the real physical characteristic every blackbody radiator has
	// (Planck's law + Wien's displacement law: low temperature -> warm/
	// orange, high temperature -> cool/blue) rather than a hand-computed
	// exact expected value - barcelona-pavilion's own night lighting uses
	// exactly this range (2500K-3500K).
	const FlatScene warmLight = flattenSource(
		"AttributeBegin\n"
		"  AreaLightSource \"diffuse\" \"blackbody L\" [ 2500 ]\n"
		+ std::string(kQuadMesh) + "AttributeEnd\n");
	const FlatScene coolLight = flattenSource(
		"AttributeBegin\n"
		"  AreaLightSource \"diffuse\" \"blackbody L\" [ 9000 ]\n"
		+ std::string(kQuadMesh) + "AttributeEnd\n");
	ASSERT_EQ(warmLight.areaLights.size(), 1u);
	ASSERT_EQ(coolLight.areaLights.size(), 1u);
	EXPECT_FALSE(warnedAbout(warmLight, "blackbody"))
		<< "a blackbody temperature is now converted, not warned-and-discarded";

	const double (&warm)[3] = warmLight.areaLights[0].L;
	const double (&cool)[3] = coolLight.areaLights[0].L;
	for (int c = 0; c < 3; ++c) {
		EXPECT_GE(warm[c], 0.0);
		EXPECT_GE(cool[c], 0.0);
	}
	EXPECT_FALSE(warm[0] == 1.0 && warm[1] == 1.0 && warm[2] == 1.0)
		<< "must not be the pre-fix flat-white {1,1,1} fallback";
	EXPECT_GT(warm[0] / warm[2], cool[0] / cool[2])
		<< "a 2500K light must read warmer (redder relative to blue) than a 9000K light";
}

TEST(FlattenMaterialTest, BlackbodyKelvinToRGBMatchesResolveEmissionColor) {
	// pbrt_flatten::blackbodyKelvinToRGB() is the factored-out core of
	// resolveEmissionColor()'s own "blackbody" branch (see that function's
	// own comment) - used directly by pbrt_cpu_builder.h's per-voxel
	// nanovdb "temperaturename" bake, which has no ParamList to hand
	// resolveEmissionColor(). Confirms the two stay in exact agreement for
	// the same temperature, and that the helper alone reproduces the same
	// warm-vs-cool physical relationship as the test just above.
	using pbrt_flatten::blackbodyKelvinToRGB;
	using pbrt_flatten::resolveEmissionColor;

	const pbrt_scene::Vec3 direct2500 = blackbodyKelvinToRGB(2500.0f);
	const pbrt_scene::Vec3 direct9000 = blackbodyKelvinToRGB(9000.0f);

	pbrt_scene::ParamList params;
	pbrt_scene::Param p;
	p.name = "L"; p.type = "blackbody"; p.numbers = {2500.0};
	params.items.push_back(p);
	const pbrt_scene::Vec3 viaResolve =
		resolveEmissionColor(params, "L", pbrt_scene::Vec3{1, 1, 1});

	EXPECT_DOUBLE_EQ(direct2500.x, viaResolve.x);
	EXPECT_DOUBLE_EQ(direct2500.y, viaResolve.y);
	EXPECT_DOUBLE_EQ(direct2500.z, viaResolve.z);

	EXPECT_GT(direct2500.x / direct2500.z, direct9000.x / direct9000.z)
		<< "2500K must read warmer than 9000K";
}

TEST(FlattenMaterialTest, BlackbodyKelvinToRGBZeroForNonPositiveTemperature) {
	const pbrt_scene::Vec3 rgb = pbrt_flatten::blackbodyKelvinToRGB(0.0f);
	EXPECT_DOUBLE_EQ(rgb.x, 0.0);
	EXPECT_DOUBLE_EQ(rgb.y, 0.0);
	EXPECT_DOUBLE_EQ(rgb.z, 0.0);
}

TEST(FlattenMaterialTest, ColorSpaceDirectiveDoesNotChangeABlackbody) {
	// A blackbody is a physical spectrum and the picture comes out in the film's colour
	// space: real pbrt-v4 draws a blackbody light identically with and without
	// ColorSpace "rec2020" (colorspace-blackbody.pbrt against blackbody-light.pbrt:
	// means 2.0724 1.2294 1.0335 both). The directive used to convert it into the
	// scene's colour space, which read red 5-11% low and green 10% high on every backend.
	const FlatScene srgbLight = flattenSource(
		"AttributeBegin\n"
		"  AreaLightSource \"diffuse\" \"blackbody L\" [ 4000 ]\n"
		+ std::string(kQuadMesh) + "AttributeEnd\n");
	const FlatScene rec2020Light = flattenSource(
		"ColorSpace \"rec2020\"\n"
		"AttributeBegin\n"
		"  AreaLightSource \"diffuse\" \"blackbody L\" [ 4000 ]\n"
		+ std::string(kQuadMesh) + "AttributeEnd\n");
	ASSERT_EQ(srgbLight.areaLights.size(), 1u);
	ASSERT_EQ(rec2020Light.areaLights.size(), 1u);
	const double (&srgb)[3] = srgbLight.areaLights[0].L;
	const double (&rec2020)[3] = rec2020Light.areaLights[0].L;
	for (int c = 0; c < 3; ++c)
		EXPECT_DOUBLE_EQ(srgb[c], rec2020[c]) << "channel " << c << ": a blackbody does not depend on the scene's colour space";
}

TEST(FlattenPunctualLightTest, PointLightBlackbodyIntensityIsConverted) {
	// Punctual lights (point/spot/distant/goniometric) previously had NO
	// blackbody handling at all - not even a warning, "blackbody I" silently
	// became flat white with zero diagnostic. resolveEmissionColor() now
	// covers these the same way as AreaLightSource.
	const FlatScene s = flattenSource(
		"LightSource \"point\" \"point3 from\" [ 0 0 0 ] \"blackbody I\" [ 3000 ]\n");
	ASSERT_EQ(s.punctualLights.size(), 1u);
	const double (&i)[3] = s.punctualLights[0].intensity;
	for (int c = 0; c < 3; ++c) EXPECT_GE(i[c], 0.0);
	EXPECT_FALSE(i[0] == 1.0 && i[1] == 1.0 && i[2] == 1.0)
		<< "must not be the pre-fix flat-white {1,1,1} fallback";
	EXPECT_GT(i[0], i[2]) << "a 3000K light must read warmer (more red than blue)";
}

TEST(FlattenMaterialTest, MaterialIndicesOnGeometryStillLineUp) {
	const FlatScene s = flattenSource(
		"Material \"diffuse\"\n" + std::string(kQuadMesh)
		+ "Material \"conductor\"\n" + std::string(kQuadMesh));
	ASSERT_EQ(s.materials.size(), 2u);
	ASSERT_EQ(s.triangles.size(), 4u);
	EXPECT_EQ(s.materials[s.triangles[0].material].kind, MaterialKind::Diffuse);
	EXPECT_EQ(s.materials[s.triangles[3].material].kind, MaterialKind::Conductor);
}

// ---------------------------------------------------------------------------
// Material "mix"
// ---------------------------------------------------------------------------
// A stochastic blend of two OTHER named materials (pbrt-v4 MixMaterial).
// src/TheRestOfYourLife/material_pbrt.h's `class mix_material` already backs
// this generically (added earlier for SPPM support, but not SPPM-specific -
// see MaterialKind::Mix's own comment) - these pin the name -> index
// resolution flatten() has to do to bridge onto it: forward AND backward
// references, and the "malformed mix" cases downgrading to the existing
// diffuse-fallback tier rather than a new, separate failure mode.

TEST(FlattenMaterialTest, MixResolvesBothNamedSubMaterials) {
	const FlatScene s = flattenSource(
		"MakeNamedMaterial \"a\" \"string type\" [ \"diffuse\" ] \"rgb reflectance\" [ 1 0 0 ]\n"
		"MakeNamedMaterial \"b\" \"string type\" [ \"conductor\" ] \"rgb reflectance\" [ 0 0 1 ]\n"
		"Material \"mix\" \"string materials\" [ \"a\" \"b\" ] \"float amount\" [ 0.25 ]\n"
		+ std::string(kQuadMesh));
	ASSERT_EQ(s.materials.size(), 3u);
	const Material &mix = s.materials[2];
	EXPECT_EQ(mix.kind, MaterialKind::Mix);
	ASSERT_GE(mix.mixMaterialA, 0);
	ASSERT_GE(mix.mixMaterialB, 0);
	EXPECT_EQ(s.materials[mix.mixMaterialA].kind, MaterialKind::Diffuse);
	EXPECT_EQ(s.materials[mix.mixMaterialB].kind, MaterialKind::Conductor);
	EXPECT_DOUBLE_EQ(mix.mixWeight, 0.25);
	EXPECT_FALSE(warnedAbout(s, "not supported"));
}

TEST(FlattenMaterialTest, MixCanForwardReferenceAMaterialDeclaredLater) {
	// pbrt-v4 resolves MixMaterial's "materials" names against the whole
	// scene, not just what came textually before the mix directive itself -
	// this is the reason flatten() builds its name->index map in a pre-pass
	// rather than incrementally during the main per-material loop.
	const FlatScene s = flattenSource(
		"Material \"mix\" \"string materials\" [ \"later\" \"also_later\" ]\n"
		"MakeNamedMaterial \"later\" \"string type\" [ \"diffuse\" ]\n"
		"MakeNamedMaterial \"also_later\" \"string type\" [ \"conductor\" ]\n"
		+ std::string(kQuadMesh));
	ASSERT_EQ(s.materials.size(), 3u);
	const Material &mix = s.materials[0];
	EXPECT_EQ(mix.kind, MaterialKind::Mix);
	ASSERT_GE(mix.mixMaterialA, 0);
	ASSERT_GE(mix.mixMaterialB, 0);
	EXPECT_EQ(s.materials[mix.mixMaterialA].kind, MaterialKind::Diffuse);
	EXPECT_EQ(s.materials[mix.mixMaterialB].kind, MaterialKind::Conductor);
}

TEST(FlattenMaterialTest, MixAmountDefaultsToOneHalf) {
	const FlatScene s = flattenSource(
		"MakeNamedMaterial \"a\" \"string type\" [ \"diffuse\" ]\n"
		"MakeNamedMaterial \"b\" \"string type\" [ \"diffuse\" ]\n"
		"Material \"mix\" \"string materials\" [ \"a\" \"b\" ]\n"
		+ std::string(kQuadMesh));
	ASSERT_EQ(s.materials.size(), 3u);
	EXPECT_DOUBLE_EQ(s.materials[2].mixWeight, 0.5);
}

TEST(FlattenMaterialTest, MixWithAnUnknownNameFallsBackToDiffuseAndWarns) {
	const FlatScene s = flattenSource(
		"MakeNamedMaterial \"a\" \"string type\" [ \"diffuse\" ]\n"
		"Material \"mix\" \"string materials\" [ \"a\" \"does_not_exist\" ]\n"
		+ std::string(kQuadMesh));
	ASSERT_EQ(s.materials.size(), 2u);
	EXPECT_EQ(s.materials[1].kind, MaterialKind::Unsupported);
	EXPECT_TRUE(warnedAbout(s, "mix"));
}

TEST(FlattenMaterialTest, MixWithFewerThanTwoNamesFallsBackToDiffuseAndWarns) {
	const FlatScene s = flattenSource(
		"MakeNamedMaterial \"a\" \"string type\" [ \"diffuse\" ]\n"
		"Material \"mix\" \"string materials\" [ \"a\" ]\n"
		+ std::string(kQuadMesh));
	ASSERT_EQ(s.materials.size(), 2u);
	EXPECT_EQ(s.materials[1].kind, MaterialKind::Unsupported);
	EXPECT_TRUE(warnedAbout(s, "mix"));
}

// ---------------------------------------------------------------------------
// Focus distance
// ---------------------------------------------------------------------------
// focusDistanceFor() exists because pbrt's "no depth of field" default is the
// sentinel 1e6, and our camera uses focus_dist to size the viewport as well as
// to place the plane of focus. Handing it the sentinel scales the primary ray
// direction by a million, and the fixed t_min of 0.001 then rejects every hit
// within a thousand world units - which deleted near geometry from every
// loaded scene and looked like a broken material.

TEST(PbrtFocusTest, TheNoDepthOfFieldSentinelNeverReachesTheCamera) {
	pbrt_flatten::Camera c;                       // defaults: aperture 0, 1e6
	c.lookfrom[2] = -800; c.lookat[2] = 0;
	EXPECT_LT(pbrt_flatten::focusDistanceFor(c), 1e5)
		<< "pbrt's 1e6 sentinel was passed through as if it were a measurement";
}

TEST(PbrtFocusTest, WithNoApertureItFocusesOnTheSubject) {
	// Nothing is out of focus without an aperture, so the only requirement is
	// a sane viewport scale - and the distance to what the camera is aimed at
	// is the one number guaranteed to be on the scene's own scale.
	pbrt_flatten::Camera c;
	c.lookfrom[0] = 278; c.lookfrom[1] = 278; c.lookfrom[2] = -800;
	c.lookat[0] = 278;   c.lookat[1] = 278;   c.lookat[2] = 0;
	EXPECT_NEAR(pbrt_flatten::focusDistanceFor(c), 800.0, 1e-9);
}

TEST(PbrtFocusTest, AnExplicitFocalDistanceIsHonouredWhenThereIsAnAperture) {
	pbrt_flatten::Camera c;
	c.lookfrom[2] = -10; c.lookat[2] = 0;
	c.aperture = 0.5;
	c.focusDistance = 37.0;
	EXPECT_NEAR(pbrt_flatten::focusDistanceFor(c), 37.0, 1e-9)
		<< "a scene that set focaldistance meant it";
}

TEST(PbrtFocusTest, AnApertureWithNoFocalDistanceStillFallsBackRatherThanUsing1e6) {
	// pbrt lets a scene set lensradius without focaldistance, leaving the
	// sentinel in place. Honouring it literally would reintroduce the bug on
	// exactly the scenes that asked for depth of field.
	pbrt_flatten::Camera c;
	c.lookfrom[2] = -10; c.lookat[2] = 0;
	c.aperture = 0.5;
	EXPECT_NEAR(pbrt_flatten::focusDistanceFor(c), 10.0, 1e-9);
}

TEST(PbrtFocusTest, ADegenerateCameraStillYieldsAUsableDistance) {
	// lookfrom == lookat is nonsense, but returning 0 would make the viewport
	// zero-sized and every ray degenerate. Better a harmless default.
	pbrt_flatten::Camera c;
	for (int i = 0; i < 3; ++i) { c.lookfrom[i] = 5; c.lookat[i] = 5; }
	EXPECT_GT(pbrt_flatten::focusDistanceFor(c), 0.0);
}
