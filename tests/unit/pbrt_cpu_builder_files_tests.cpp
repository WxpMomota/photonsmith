/**
 * @file pbrt_cpu_builder_files_tests.cpp
 * @brief pbrt_cpu builder tests: scenes that read files (textures, PLY, includes) and the rest of the builder
 * Helpers are in pbrt_cpu_builder_test_helpers.h; one part of what used to be a single pbrt_cpu_builder_tests.cpp.
 */

#include "pbrt_cpu_builder_test_helpers.h"

TEST_F(CpuBuilderTempTree, DisplacementWithARealGrayscaleBumpMapWrapsInBumpMapMaterial) {
	// Round 5 Phase 1: Material::displacementTextureFilename (see that
	// field's own comment) must reach the CPU builder's materialFor()
	// lambda and wrap the base material in bump_map_material - the same
	// decorator mesh.h's own OBJ/MTL map_Bump dispatch already uses for a
	// genuine grayscale height/displacement image. Goes through the real
	// pbrt_load::loadFile() resolution pass (not just flatten()) so the
	// path is a real, existence-tested, absolute file pbrt_cpu_builder.h's
	// rtw_image probe can actually decode.
	write("scene.pbrt",
		  "Texture \"bmap\" \"float\" \"imagemap\" \"string filename\" [ \"bump.bmp\" ]\n"
		  "Material \"diffuse\" \"rgb reflectance\" [ .5 .5 .5 ] "
		  "\"texture displacement\" [ \"bmap\" ]\n"
		  + std::string(kQuad));
	write("bump.bmp", solidGrayBmp1x1());

	const pbrt_load::LoadResult loaded = pbrt_load::loadFile(path("scene.pbrt"));
	ASSERT_TRUE(loaded.ok) << loaded.error;
	ASSERT_EQ(loaded.scene.materials.size(), 1u);
	ASSERT_EQ(loaded.scene.materials[0].displacementTextureFilename, path("bump.bmp"));

	const pbrt_cpu::BuildResult b = pbrt_cpu::build(loaded.scene);
	hit_record rec;
	ASSERT_TRUE(b.world->hit(ray(point3(0.5, 0.5, -5), vec3(0, 0, 1)),
							 interval(0.001, infinity), rec));
	EXPECT_NE(dynamic_cast<bump_map_material *>(rec.mat.get()), nullptr)
		<< "a material with a resolved, decodable displacement texture must "
		   "wrap the base material in bump_map_material";
}

// A 1x1 24bpp BMP of one colour (r, g, b), written as the file stores it (BGR order).
static std::string solidBmp1x1Rgb(unsigned char r, unsigned char g, unsigned char b) {
	std::string bytes;
	const auto u16 = [&](unsigned short v) {
		bytes.push_back(static_cast<char>(v & 0xFF));
		bytes.push_back(static_cast<char>((v >> 8) & 0xFF));
	};
	const auto u32 = [&](unsigned int v) {
		bytes.push_back(static_cast<char>(v & 0xFF));
		bytes.push_back(static_cast<char>((v >> 8) & 0xFF));
		bytes.push_back(static_cast<char>((v >> 16) & 0xFF));
		bytes.push_back(static_cast<char>((v >> 24) & 0xFF));
	};
	bytes.push_back('B'); bytes.push_back('M');
	u32(14 + 40 + 4);
	u32(0);
	u32(14 + 40);
	u32(40);
	u32(1);
	u32(1);
	u16(1);
	u16(24);
	u32(0);
	u32(4);
	u32(0); u32(0);
	u32(0); u32(0);
	bytes.push_back(static_cast<char>(b));
	bytes.push_back(static_cast<char>(g));
	bytes.push_back(static_cast<char>(r));
	bytes.push_back('\0');
	return bytes;
}

// A height map is a float imagemap: pbrt decodes an 8-bit image as sRGB and interpolates the decoded FLOATS. The CPU used to store the
// decoded value requantized to a byte, which crushes the darks (every byte value up to ~12 decodes below 1/255 and rounded to 0 or 1/255),
// so the finite-difference slope of a smooth dark ramp came out as stairs - bump-mapped-plane read 1-5% off the GPU, which decodes per texel.
TEST_F(CpuBuilderTempTree, BumpMapKeepsTheSrgbDecodedHeightAsAFloat) {
	write("scene.pbrt",
		  "Texture \"bmap\" \"float\" \"imagemap\" \"string filename\" [ \"dark.bmp\" ]\n"
		  "Material \"diffuse\" \"rgb reflectance\" [ .5 .5 .5 ] \"texture displacement\" [ \"bmap\" ]\n"
		  + std::string(kQuad));
	write("dark.bmp", solidBmp1x1Rgb(10, 10, 10));
	const pbrt_load::LoadResult loaded = pbrt_load::loadFile(path("scene.pbrt"));
	ASSERT_TRUE(loaded.ok) << loaded.error;
	const pbrt_cpu::BuildResult b = pbrt_cpu::build(loaded.scene);
	hit_record rec;
	ASSERT_TRUE(b.world->hit(ray(point3(0.5, 0.5, -5), vec3(0, 0, 1)), interval(0.001, infinity), rec));
	auto *bump = dynamic_cast<bump_map_material *>(rec.mat.get());
	ASSERT_NE(bump, nullptr);
	const double expected = 10.0 / 255.0 / 12.92;   // sRGB decode of a dark byte (the linear toe: c / 12.92)
	EXPECT_NEAR(bump->get_bump_tex()->value(0.5, 0.5, point3(0, 0, 0)).x(), expected, 2e-5)
		<< "the decoded height (0.003) must not be requantized to a byte (which would read 0 or 0.0039)";
}

// pbrt reads a tangent-space normal map LINEAR (scene.cpp: Image::Read(filename, ..., ColorEncoding::Linear)). Both backends used to sRGB-decode
// it like any other 8-bit image, which tilts every texel: a flat (128,128,255) map read as (0.22,0.22,1) -> a normal 49 degrees off the surface's.
TEST_F(CpuBuilderTempTree, NormalMapIsReadLinearNotSrgbDecoded) {
	write("scene.pbrt",
		  "Texture \"nmap\" \"float\" \"imagemap\" \"string filename\" [ \"flat.bmp\" ]\n"
		  "Material \"diffuse\" \"rgb reflectance\" [ .5 .5 .5 ] \"texture displacement\" [ \"nmap\" ]\n"
		  + std::string(kQuad));
	write("flat.bmp", solidBmp1x1Rgb(128, 128, 255));
	const pbrt_load::LoadResult loaded = pbrt_load::loadFile(path("scene.pbrt"));
	ASSERT_TRUE(loaded.ok) << loaded.error;
	const pbrt_cpu::BuildResult b = pbrt_cpu::build(loaded.scene);
	hit_record rec;
	ASSERT_TRUE(b.world->hit(ray(point3(0.5, 0.5, -5), vec3(0, 0, 1)), interval(0.001, infinity), rec));
	auto *nm = dynamic_cast<normal_map_material *>(rec.mat.get());
	ASSERT_NE(nm, nullptr) << "an RGB displacement image is a normal map";
	const color c = nm->get_normal_tex()->value(0.5, 0.5, point3(0, 0, 0));
	EXPECT_NEAR(c.x(), 128.0 / 255.0, 1e-4);
	EXPECT_NEAR(c.y(), 128.0 / 255.0, 1e-4);
	EXPECT_NEAR(c.z(), 1.0, 1e-4);
}

TEST(PbrtCpuBuildTest, DiffuseTransmissionBuildsTheRealMaterialNotLambertian) {
	const pbrt_cpu::BuildResult b = buildFrom(
		"Material \"diffusetransmission\"\n" + std::string(kQuad));
	hit_record rec;
	ASSERT_TRUE(b.world->hit(ray(point3(0.5, 0.5, -5), vec3(0, 0, 1)),
							 interval(0.001, infinity), rec));
	EXPECT_NE(dynamic_cast<diffuse_transmission *>(rec.mat.get()), nullptr)
		<< "a pbrt diffusetransmission material must build the real "
		   "diffuse_transmission class, not silently fall back to lambertian";
}

TEST(PbrtCpuBuildTest, DiffuseTransmissionReflectanceScaleWrappedImagemapAppliesTheScale) {
	// "scale"-unwrap used to be CoatedDiffuse-only; now also resolved for
	// DiffuseTransmission's own reflectance - Material::textureScale's own
	// comment.
	const pbrt_cpu::BuildResult b = buildFrom(
		"Texture \"leaf\" \"spectrum\" \"imagemap\" \"string filename\" [ \"leaf.png\" ]\n"
		"Texture \"leaf-scaled\" \"spectrum\" \"scale\" \"texture tex\" [ \"leaf\" ] "
		"\"float scale\" [ 0.5 ]\n"
		"Material \"diffusetransmission\" \"texture reflectance\" [ \"leaf-scaled\" ]\n"
		+ std::string(kQuad));
	hit_record rec;
	ASSERT_TRUE(b.world->hit(ray(point3(0.5, 0.5, -5), vec3(0, 0, 1)),
							 interval(0.001, infinity), rec));
	auto *dt = dynamic_cast<diffuse_transmission *>(rec.mat.get());
	ASSERT_NE(dt, nullptr);
	auto *scaled = dynamic_cast<scaled_texture *>(dt->get_reflectance_texture().get());
	ASSERT_NE(scaled, nullptr)
		<< "a scale-wrapped imagemap reflectance must build a scaled_texture "
		   "wrapping the real mipmap_texture, not just the bare image";
}

TEST(PbrtCpuBuildTest, DiffuseTransmissionTransmittanceScaleWrappedImagemapAppliesItsOwnScale) {
	// Same as the reflectance test above, for transmittance's own
	// independent scale (Material::transmittanceTextureScale's own
	// comment) - a DIFFERENT scale value than reflectance's own, to catch
	// the two scales being accidentally cross-applied.
	const pbrt_cpu::BuildResult b = buildFrom(
		"Texture \"leaf-r\" \"spectrum\" \"imagemap\" \"string filename\" [ \"leaf-r.png\" ]\n"
		"Texture \"leaf-t\" \"spectrum\" \"imagemap\" \"string filename\" [ \"leaf-t.png\" ]\n"
		"Texture \"leaf-t-scaled\" \"spectrum\" \"scale\" \"texture tex\" [ \"leaf-t\" ] "
		"\"float scale\" [ 0.5 ]\n"
		"Material \"diffusetransmission\" \"texture reflectance\" [ \"leaf-r\" ] "
		"\"texture transmittance\" [ \"leaf-t-scaled\" ]\n"
		+ std::string(kQuad));
	hit_record rec;
	ASSERT_TRUE(b.world->hit(ray(point3(0.5, 0.5, -5), vec3(0, 0, 1)),
							 interval(0.001, infinity), rec));
	auto *dt = dynamic_cast<diffuse_transmission *>(rec.mat.get());
	ASSERT_NE(dt, nullptr);
	EXPECT_EQ(dynamic_cast<scaled_texture *>(dt->get_reflectance_texture().get()), nullptr)
		<< "reflectance was bound to a bare (unscaled) imagemap, must not be scaled_texture-wrapped";
	auto *scaled = dynamic_cast<scaled_texture *>(dt->get_transmittance_texture().get());
	ASSERT_NE(scaled, nullptr)
		<< "a scale-wrapped imagemap transmittance must build a scaled_texture "
		   "wrapping the real mipmap_texture, not just the bare image";
}

// ---------------------------------------------------------------------------
// LightSource point/spot/distant/goniometric/projection
//
// pbrt_flatten_tests.cpp already pins the parsing (parameter defaults, CTM
// handling); these pin the next stage - that a parsed pbrt_flatten::
// PunctualLight actually becomes a real punctual_light_objects.h light a
// shading point can sample, wired the same way build_point_light_punct() et
// al. wire the hand-built C2-C6 showcase scenes (scenes_advanced.h).
// ---------------------------------------------------------------------------

TEST(PbrtCpuBuildTest, NoLightSourceLeavesPunctLightsNull) {
	const pbrt_cpu::BuildResult b = buildFrom(kQuad);
	EXPECT_EQ(b.punctLights, nullptr);
}

TEST(PbrtCpuBuildTest, PointLightSourceIsSampleableAtAShadingPoint) {
	const pbrt_cpu::BuildResult b = buildFrom(
		"LightSource \"point\" \"point3 from\" [ 0 10 0 ] \"rgb I\" [ 1 1 1 ] "
		"\"float scale\" [ 100 ]\n" + std::string(kQuad));
	ASSERT_NE(b.punctLights, nullptr);
	ASSERT_FALSE(b.punctLights->empty());

	int samples = 0;
	b.punctLights->for_each_sample(point3(0, 0, 0), [&](const PunctualLiSample &s) {
		++samples;
		// wi must point from the shading point TOWARD the light: straight up.
		EXPECT_NEAR(s.wi.x(), 0.0, 1e-9);
		EXPECT_NEAR(s.wi.y(), 1.0, 1e-9);
		EXPECT_NEAR(s.wi.z(), 0.0, 1e-9);
		// Li = I * scale / r^2 = 1 * 100 / 100 = 1.
		EXPECT_NEAR(s.Li.x(), 1.0, 1e-6);
		EXPECT_NEAR(s.t_max, 10.0, 1e-6);
	});
	EXPECT_EQ(samples, 1);
}

TEST(PbrtCpuBuildTest, SpotLightSourceAimsFromFromTowardTo) {
	const pbrt_cpu::BuildResult b = buildFrom(
		"LightSource \"spot\" \"point3 from\" [ 0 0 -10 ] \"point3 to\" [ 0 0 0 ] "
		"\"rgb I\" [ 1 1 1 ] \"float scale\" [ 100 ] \"float coneangle\" [ 60 ]\n"
		+ std::string(kQuad));
	ASSERT_NE(b.punctLights, nullptr);

	// A point directly in front of the spot (along its axis) sees full
	// intensity; the shadow-ray direction must point back toward the light.
	int samples = 0;
	b.punctLights->for_each_sample(point3(0, 0, 0), [&](const PunctualLiSample &s) {
		++samples;
		EXPECT_NEAR(s.wi.z(), -1.0, 1e-9);
		EXPECT_GT(s.Li.x(), 0.0) << "on-axis point should be lit, not in the falloff";
	});
	EXPECT_EQ(samples, 1);
}

TEST(PbrtCpuBuildTest, DistantLightSourceHasNoDistanceFalloff) {
	const pbrt_cpu::BuildResult b = buildFrom(
		"LightSource \"distant\" \"point3 from\" [ 0 1 0 ] \"point3 to\" [ 0 0 0 ] "
		"\"rgb L\" [ 2 3 4 ] \"float scale\" [ 1 ]\n" + std::string(kQuad));
	ASSERT_NE(b.punctLights, nullptr);

	// Radiance must be identical at two very different distances - that is
	// the entire point of a directional/parallel light.
	color nearL, farL;
	b.punctLights->for_each_sample(point3(0, 0, 0),
		[&](const PunctualLiSample &s) { nearL = s.Li; });
	b.punctLights->for_each_sample(point3(0, 1000, 0),
		[&](const PunctualLiSample &s) { farL = s.Li; });
	EXPECT_NEAR(nearL.x(), 2.0, 1e-6);
	EXPECT_NEAR(nearL.x(), farL.x(), 1e-9);
	EXPECT_NEAR(nearL.y(), farL.y(), 1e-9);
	EXPECT_NEAR(nearL.z(), farL.z(), 1e-9);
}

TEST(PbrtCpuBuildTest, GoniometricLightSourceIsSampleableAtAShadingPoint) {
	// pbrt-v4's GoniometricLight has no "from"/"to" of its own (unlike point/
	// spot/distant) - its position/orientation come purely from the CTM, so
	// this positions it with Translate rather than a "from" parameter (see
	// PunctualLight::pos's own comment). No "filename" - flatten() falls
	// back to an isotropic profile (see FlattenPunctualLightTest::
	// GoniometricLightWithNoFilenameIsIsotropicAndUnwarned in
	// pbrt_flatten_tests.cpp), so this should behave exactly like a plain
	// point light: nonzero, direction-independent intensity.
	const pbrt_cpu::BuildResult b = buildFrom(
		"Translate 0 10 0\n"
		"LightSource \"goniometric\" \"rgb I\" [ 1 1 1 ] \"float scale\" [ 100 ]\n"
		+ std::string(kQuad));
	ASSERT_NE(b.punctLights, nullptr);

	int samples = 0;
	b.punctLights->for_each_sample(point3(0, 0, 0), [&](const PunctualLiSample &s) {
		++samples;
		EXPECT_GT(s.Li.x(), 0.0);
	});
	EXPECT_EQ(samples, 1);
}

TEST_F(CpuBuilderTempTree, GoniometricLightWithRealSquareImageProducesNonUniformIntensity) {
	// Goes through the real pbrt_load::loadFile() resolution pass (not
	// buildFrom()'s bare flatten()) so PunctualLight::filename is a real,
	// existence-tested, absolute path pbrt_cpu_builder.h's decode can
	// actually open - see decodePunctualLightImageFile()'s own comment.
	write("scene.pbrt",
		  "LightSource \"goniometric\" \"rgb I\" [ 1 1 1 ] \"float scale\" [ 100 ] "
		  "\"string filename\" [ \"profile.bmp\" ]\n");
	write("profile.bmp", twoToneBmp2x2());

	const pbrt_load::LoadResult loaded = pbrt_load::loadFile(path("scene.pbrt"));
	ASSERT_TRUE(loaded.ok) << loaded.error;
	ASSERT_EQ(loaded.scene.punctualLights.size(), 1u);
	ASSERT_EQ(loaded.scene.punctualLights[0].filename, path("profile.bmp"));

	const pbrt_cpu::BuildResult b = pbrt_cpu::build(loaded.scene);
	ASSERT_NE(b.punctLights, nullptr);

	// Sample from points spread across genuinely different directions
	// (not just a flat circle at one height, which - since the two-tone
	// split is row-wise, i.e. along the equal-area square's v/polar axis -
	// would keep every point in the same row band and defeat this test) -
	// a real two-tone image gives a different eval_I() at different
	// directions; the flat kUniformImage fallback would give the exact
	// same value everywhere, since telling those two apart is the whole
	// point of this test.
	const point3 samplePoints[] = {
		point3(10, 0, 0),  point3(-10, 0, 0),
		point3(0, 10, 0),  point3(0, -10, 0),
		point3(0, 0, 10),  point3(0, 0, -10),
		point3(7, 7, 7),   point3(-7, -7, -7),
	};
	double maxLi = 0.0;
	bool sawDifferentValue = false;
	bool sawFirst = false;
	double first = 0.0;
	for (const point3 &p : samplePoints) {
		b.punctLights->for_each_sample(p, [&](const PunctualLiSample &s) {
			maxLi = std::max(maxLi, s.Li.x());
			if (!sawFirst) { first = s.Li.x(); sawFirst = true; }
			else if (std::abs(s.Li.x() - first) > 1e-9) sawDifferentValue = true;
		});
	}
	EXPECT_GT(maxLi, 0.0);
	EXPECT_TRUE(sawDifferentValue)
		<< "identical intensity at every sampled direction means the real "
		   "two-tone image was not actually used";
}

TEST_F(CpuBuilderTempTree, ProjectionLightWithRealImageProducesNonUniformIntensity) {
	write("scene.pbrt",
		  "Translate 0 0 -10\n"
		  "LightSource \"projection\" \"float scale\" [ 100 ] \"float fov\" [ 80 ] "
		  "\"string filename\" [ \"slide.bmp\" ]\n");
	write("slide.bmp", twoToneBmp2x2());

	const pbrt_load::LoadResult loaded = pbrt_load::loadFile(path("scene.pbrt"));
	ASSERT_TRUE(loaded.ok) << loaded.error;
	ASSERT_EQ(loaded.scene.punctualLights.size(), 1u);
	ASSERT_EQ(loaded.scene.punctualLights[0].filename, path("slide.bmp"));

	const pbrt_cpu::BuildResult b = pbrt_cpu::build(loaded.scene);
	ASSERT_NE(b.punctLights, nullptr);

	// Sample across a small grid of points inside the projected beam - a
	// real two-tone slide gives different Li at different points within
	// its fov; kUniformSlide's flat white fallback would give the exact
	// same value everywhere inside the beam.
	double first = -1.0;
	bool sawDifferentValue = false;
	for (int gx = -2; gx <= 2; ++gx) {
		for (int gy = -2; gy <= 2; ++gy) {
			const point3 p(gx * 0.5, gy * 0.5, 0.0);
			b.punctLights->for_each_sample(p, [&](const PunctualLiSample &s) {
				if (s.Li.x() <= 0.0) return;  // outside the beam/fov
				if (first < 0.0) first = s.Li.x();
				else if (std::abs(s.Li.x() - first) > 1e-9) sawDifferentValue = true;
			});
		}
	}
	EXPECT_GT(first, 0.0);
	EXPECT_TRUE(sawDifferentValue)
		<< "identical intensity at every sampled point means the real "
		   "two-tone slide was not actually used";
}

TEST(PbrtCpuBuildTest, ProjectionLightSourceIsSampleableAtAShadingPoint) {
	// Like GoniometricLight, pbrt-v4's ProjectionLight has no "from"/"to" -
	// position/aim come purely from the CTM (Translate here places it at
	// (0,0,-10) looking down its local +z, which with no Rotate is world
	// +z - straight at the quad's shading point at the origin). No
	// "filename" either (see flatten()'s own warning for this case) - the
	// uniform-white-beam fallback should still light a point inside its fov.
	const pbrt_cpu::BuildResult b = buildFrom(
		"Translate 0 0 -10\n"
		"LightSource \"projection\" \"float scale\" [ 100 ] \"float fov\" [ 60 ]\n"
		+ std::string(kQuad));
	ASSERT_NE(b.punctLights, nullptr);

	int samples = 0;
	b.punctLights->for_each_sample(point3(0, 0, 0), [&](const PunctualLiSample &s) {
		++samples;
		EXPECT_GT(s.Li.x(), 0.0) << "on-axis point should fall inside the projected beam";
	});
	EXPECT_EQ(samples, 1);
}

// ---------------------------------------------------------------------------
// Material "dielectric" with/without roughness
//
// A nonzero "roughness" on a pbrt dielectric asks for the GGX microfacet
// variant (pbrt-v4 DielectricBxDF's rough path) - this codebase's real model
// for that is rough_dielectric, not plain dielectric. Pins that the roughness
// parameter actually routes to the different class rather than being parsed
// and silently dropped (a pre-existing gap fixed alongside the Metal/
// rough_metal GPU mismatch this session).
// ---------------------------------------------------------------------------

TEST(PbrtCpuBuildTest, SmoothDielectricBuildsThePlainDielectricClass) {
	const pbrt_cpu::BuildResult b = buildFrom(
		"Material \"dielectric\" \"float eta\" [ 1.5 ]\n" + std::string(kQuad));
	hit_record rec;
	ASSERT_TRUE(b.world->hit(ray(point3(0.5, 0.5, -5), vec3(0, 0, 1)),
							 interval(0.001, infinity), rec));
	EXPECT_NE(dynamic_cast<dielectric *>(rec.mat.get()), nullptr)
		<< "a pbrt dielectric with no roughness must build the plain smooth "
		   "dielectric class";
	EXPECT_EQ(dynamic_cast<rough_dielectric *>(rec.mat.get()), nullptr);
}

TEST(PbrtCpuBuildTest, RoughDielectricRoughnessBuildsTheRealRoughDielectricClass) {
	const pbrt_cpu::BuildResult b = buildFrom(
		"Material \"dielectric\" \"float eta\" [ 1.5 ] \"float roughness\" [ 0.2 ]\n"
		+ std::string(kQuad));
	hit_record rec;
	ASSERT_TRUE(b.world->hit(ray(point3(0.5, 0.5, -5), vec3(0, 0, 1)),
							 interval(0.001, infinity), rec));
	EXPECT_NE(dynamic_cast<rough_dielectric *>(rec.mat.get()), nullptr)
		<< "a pbrt dielectric with nonzero roughness must build the real "
		   "rough_dielectric (GGX) class, not silently drop the roughness "
		   "and render a perfect mirror/refractor";
}

// ---------------------------------------------------------------------------
// Material "none" (pbrt-v4's real interface-material idiom)
//
// Must build the dedicated interface_material class, not a near-invisible
// dielectric - real pass-through, no Fresnel/refraction, and a distinct
// classification (is_medium_boundary()=true, is_delta_bsdf()=false) the
// integrators use to skip the crossing entirely instead of treating it as a
// specular bounce.
// ---------------------------------------------------------------------------

TEST(PbrtCpuBuildTest, InterfaceMaterialBuildsTheDedicatedClass) {
	const pbrt_cpu::BuildResult b = buildFrom(
		"Material \"none\"\n" + std::string(kQuad));
	hit_record rec;
	ASSERT_TRUE(b.world->hit(ray(point3(0.5, 0.5, -5), vec3(0, 0, 1)),
							 interval(0.001, infinity), rec));
	ASSERT_NE(dynamic_cast<interface_material *>(rec.mat.get()), nullptr)
		<< "Material \"none\" must build the dedicated interface_material "
		   "class, not a near-invisible dielectric";
	EXPECT_FALSE(rec.mat->is_delta_bsdf())
		<< "not a real delta surface - is_medium_boundary() is the correct "
		   "classification instead";
	EXPECT_TRUE(rec.mat->is_medium_boundary());
	EXPECT_TRUE(rec.mat->is_shadow_transmissive(rec));
}

TEST(PbrtCpuBuildTest, InterfaceMaterialScatterIsAnExactPassThrough) {
	const pbrt_cpu::BuildResult b = buildFrom(
		"Material \"none\"\n" + std::string(kQuad));
	hit_record rec;
	const vec3 in_dir(0.05, -0.03, 1.0);  // slightly oblique, stays inside the [0,1]x[0,1] quad from z=-5
	ASSERT_TRUE(b.world->hit(ray(point3(0.5, 0.5, -5), unit_vector(in_dir)),
							 interval(0.001, infinity), rec));
	scatter_record srec;
	ASSERT_TRUE(rec.mat->scatter(ray(point3(0.5, 0.5, -5), unit_vector(in_dir)), rec, srec));
	EXPECT_TRUE(srec.skip_pdf);
	EXPECT_TRUE(srec.is_medium_boundary);
	EXPECT_DOUBLE_EQ(srec.attenuation.x(), 1.0);
	EXPECT_DOUBLE_EQ(srec.attenuation.y(), 1.0);
	EXPECT_DOUBLE_EQ(srec.attenuation.z(), 1.0);
	// Exact same direction as the incoming ray - no Fresnel reflection, no
	// refraction, no critical angle.
	vec3 out_dir = unit_vector(srec.skip_pdf_ray.direction());
	vec3 expected = unit_vector(in_dir);
	EXPECT_NEAR(out_dir.x(), expected.x(), 1e-12);
	EXPECT_NEAR(out_dir.y(), expected.y(), 1e-12);
	EXPECT_NEAR(out_dir.z(), expected.z(), 1e-12);
}

// ---------------------------------------------------------------------------
// Material "conductor"
//
// pbrt describes a conductor's complex IOR via "spectrum eta"/"spectrum k"
// bound to a NAMED spectrum ("metal-Ag-eta"/"metal-Ag-k") - a recognized
// metal name should build the real `conductor` class (GGX + complex
// Fresnel), not the flat-albedo `metal` fuzz-mirror approximation every
// pbrt conductor silently fell back to before.
// ---------------------------------------------------------------------------

TEST(PbrtCpuBuildTest, NamedMetalSpectrumConductorBuildsTheRealConductorClass) {
	const pbrt_cpu::BuildResult b = buildFrom(
		"Material \"conductor\" \"spectrum eta\" [ \"metal-Ag-eta\" ] "
		"\"spectrum k\" [ \"metal-Ag-k\" ] \"float roughness\" [ 0.1 ]\n"
		+ std::string(kQuad));
	hit_record rec;
	ASSERT_TRUE(b.world->hit(ray(point3(0.5, 0.5, -5), vec3(0, 0, 1)),
							 interval(0.001, infinity), rec));
	EXPECT_NE(dynamic_cast<conductor *>(rec.mat.get()), nullptr)
		<< "a pbrt conductor bound to a recognized named metal spectrum "
		   "(metal-Ag-eta/metal-Ag-k) must build the real conductor (GGX + "
		   "complex Fresnel) class, not fall back to the flat-albedo metal "
		   "fuzz-mirror approximation";
}

TEST(PbrtCpuBuildTest, ExplicitRgbEtaKConductorBuildsTheRealConductorClass) {
	// This codebase's own conductor BxDF is already a plain 3-float RGB
	// model (matching its own named-spectrum table's shape), so an explicit
	// "rgb eta"/"rgb k" pair (not a named spectrum) needs no spectral
	// upsampling to build the real class - see flatten()'s own Conductor-
	// OR-CoatedConductor branch.
	const pbrt_cpu::BuildResult b = buildFrom(
		"Material \"conductor\" \"rgb eta\" [ 0.2 0.9 1.4 ] \"rgb k\" [ 3.9 2.5 2.1 ] "
		"\"float roughness\" [ 0.1 ]\n"
		+ std::string(kQuad));
	hit_record rec;
	ASSERT_TRUE(b.world->hit(ray(point3(0.5, 0.5, -5), vec3(0, 0, 1)),
							 interval(0.001, infinity), rec));
	EXPECT_NE(dynamic_cast<conductor *>(rec.mat.get()), nullptr)
		<< "a pbrt conductor bound to an explicit rgb eta/k pair must build "
		   "the real conductor (GGX + complex Fresnel) class, not fall back "
		   "to the flat-albedo metal fuzz-mirror approximation";
}

TEST(PbrtCpuBuildTest, ExplicitRgbEtaOnlyConductorUsesTheReflectanceConversion) {
	// Giving only ONE of eta/k (not both) as an explicit RGB triple does not select measured eta/k - there is no well-defined "the
	// other one defaults to what" for an arbitrary explicit pair - so, like a conductor with a plain "reflectance", it is built
	// from its reflectance (eta = 1, k solved from it) as the real GGX + complex-Fresnel conductor (not the flat-albedo fuzz mirror).
	const pbrt_cpu::BuildResult b = buildFrom(
		"Material \"conductor\" \"rgb eta\" [ 0.2 0.9 1.4 ] \"float roughness\" [ 0.1 ]\n"
		+ std::string(kQuad));
	hit_record rec;
	ASSERT_TRUE(b.world->hit(ray(point3(0.5, 0.5, -5), vec3(0, 0, 1)),
							 interval(0.001, infinity), rec));
	EXPECT_NE(dynamic_cast<conductor *>(rec.mat.get()), nullptr)
		<< "a pbrt conductor with only eta (no k) as explicit RGB is built from its reflectance as the real conductor class";
}

TEST(PbrtCpuBuildTest, CoatedConductorNamedSpectrumBuildsTheRealConductorFresnel) {
	// CoatedConductor previously never resolved a named spectrum (or an
	// explicit RGB eta/k) at all - even "metal-Au-eta" was silently ignored,
	// always going through reflectanceToConductorK()'s approximation. Gold's
	// real complex IOR has a strongly chromatic (red >> blue) normal-
	// incidence Fresnel; the approximation would stay grey/achromatic here
	// since no "reflectance" was even given (defaults to m.color's flat
	// 0.5,0.5,0.5), so this distinguishes the two paths unambiguously.
	const pbrt_cpu::BuildResult b = buildFrom(
		"Material \"coatedconductor\" \"spectrum eta\" [ \"metal-Au-eta\" ] "
		"\"spectrum k\" [ \"metal-Au-k\" ]\n"
		+ std::string(kQuad));
	hit_record rec;
	ASSERT_TRUE(b.world->hit(ray(point3(0.5, 0.5, -5), vec3(0, 0, 1)),
							 interval(0.001, infinity), rec));
	auto *cc = dynamic_cast<coated_conductor *>(rec.mat.get());
	ASSERT_NE(cc, nullptr);
	color f0 = cc->get_conductor_f0();
	EXPECT_GT(f0.x(), f0.z())
		<< "a coatedconductor bound to a recognized named metal spectrum "
		   "must build the real complex-IOR Fresnel (gold: red F0 >> blue "
		   "F0), not the pre-existing grey/achromatic approximation";
}

// pbrt's own coatedconductor parameter names, and its defaults (CoatedConductorMaterial::Create): conductor.eta/conductor.k for
// the base, interface.roughness/conductor.roughness separately (both 0), and copper when nothing is given. The base's eta and k
// are divided by the coat's IOR, so F0 at normal incidence is the conductor's Fresnel against the coat, not against vacuum.
static coated_conductor* buildCoatedConductor(const std::string& params, pbrt_cpu::BuildResult& keepAlive, hit_record& rec) {
	keepAlive = buildFrom("Material \"coatedconductor\" " + params + "\n" + std::string(kQuad));
	if (!keepAlive.world->hit(ray(point3(0.5, 0.5, -5), vec3(0, 0, 1)), interval(0.001, infinity), rec)) return nullptr;
	return dynamic_cast<coated_conductor *>(rec.mat.get());
}

TEST(PbrtCpuBuildTest, CoatedConductorWithNothingGivenIsSmoothCopperUnderGlass) {
	pbrt_cpu::BuildResult b; hit_record rec;
	auto *cc = buildCoatedConductor("", b, rec);
	ASSERT_NE(cc, nullptr);
	EXPECT_TRUE(cc->is_delta_bsdf()) << "interface.roughness and conductor.roughness both default to 0: a mirror under glass";
	EXPECT_NEAR(cc->get_coat_ior(), 1.5, 1e-12);
	const color f0 = cc->get_conductor_f0();
	EXPECT_GT(f0.x(), f0.z()) << "pbrt's default conductor is copper: red F0 above blue";
	EXPECT_GT(f0.x(), 0.7);
}

TEST(PbrtCpuBuildTest, CoatedConductorReadsPbrtParameterNames) {
	pbrt_cpu::BuildResult b; hit_record rec;
	auto *cc = buildCoatedConductor(
		"\"rgb conductor.eta\" [ 0.143 0.375 1.442 ] \"rgb conductor.k\" [ 3.983 2.386 1.603 ] "
		"\"float interface.eta\" [ 1.33 ] \"float interface.roughness\" [ 0.2 ] \"float conductor.roughness\" [ 0.0 ]", b, rec);
	ASSERT_NE(cc, nullptr);
	EXPECT_NEAR(cc->get_coat_ior(), 1.33, 1e-12) << "interface.eta is the coat's IOR, not the conductor's";
	EXPECT_NEAR(cc->get_coat_roughness(), 0.2, 1e-9);
	EXPECT_FALSE(cc->is_delta_bsdf()) << "a rough coat is glossy even over a smooth conductor";
	// eta and k are relative to the coat: gold's blue channel F0 against vacuum is 0.32, against a 1.33 coat it is lower.
	const color f0 = cc->get_conductor_f0();
	EXPECT_GT(f0.x(), 0.9);
	EXPECT_LT(f0.z(), 0.32);
}

TEST(PbrtCpuBuildTest, CoatedConductorBareRoughnessStillMeansBothInterfaces) {
	// The older spelling (one "roughness"): pbrt would reject it as an unused parameter, this loader keeps it as the roughness of
	// both interfaces. A smooth coat with only conductor.roughness is glossy too.
	pbrt_cpu::BuildResult b1, b2; hit_record r1, r2;
	auto *legacy = buildCoatedConductor("\"float roughness\" [ 0.3 ]", b1, r1);
	auto *baseOnly = buildCoatedConductor("\"float conductor.roughness\" [ 0.3 ]", b2, r2);
	ASSERT_NE(legacy, nullptr);
	ASSERT_NE(baseOnly, nullptr);
	EXPECT_FALSE(legacy->is_delta_bsdf());
	EXPECT_NEAR(legacy->get_coat_roughness(), 0.3, 1e-9);
	EXPECT_FALSE(baseOnly->is_delta_bsdf());
	EXPECT_NEAR(baseOnly->get_coat_roughness(), 0.0, 1e-12) << "interface.roughness defaults to 0 once the pbrt names are used";
}

TEST(PbrtCpuBuildTest, ReflectanceConductorBuildsTheRealConductorClass) {
	const pbrt_cpu::BuildResult b = buildFrom(
		"Material \"conductor\" \"rgb reflectance\" [ .8 .8 .8 ] "
		"\"float roughness\" [ 0.1 ]\n"
		+ std::string(kQuad));
	hit_record rec;
	ASSERT_TRUE(b.world->hit(ray(point3(0.5, 0.5, -5), vec3(0, 0, 1)),
							 interval(0.001, infinity), rec));
	// pbrt-v4 turns "rgb reflectance" into eta = 1 and k = 2 sqrt(r) / sqrt(1 - r); the old flat-albedo `metal` (roughness as a
	// mirror fuzz) had no glossy lobe, so a point or spot light could not make a highlight on it.
	EXPECT_NE(dynamic_cast<conductor *>(rec.mat.get()), nullptr)
		<< "a pbrt conductor given only a reflectance must build the real GGX + complex-Fresnel conductor class";
	EXPECT_EQ(dynamic_cast<metal *>(rec.mat.get()), nullptr);
}

// ---------------------------------------------------------------------------
// Material "mix"
//
// pbrt_flatten_tests.cpp pins the name resolution; these pin the next stage -
// that a resolved pbrt_flatten::Material with MaterialKind::Mix actually
// becomes a real mix_material wrapping the real CPU classes its two named
// sub-materials asked for (not two more Lambertians), matching how the
// Coated/DiffuseTransmission tests above already pin their own real classes.
// ---------------------------------------------------------------------------

TEST(PbrtCpuBuildTest, MixMaterialBuildsTheRealMixOfItsTwoSubMaterials) {
	const pbrt_cpu::BuildResult b = buildFrom(
		"MakeNamedMaterial \"a\" \"string type\" [ \"conductor\" ] \"rgb reflectance\" [ .2 .4 .6 ]\n"
		"MakeNamedMaterial \"b\" \"string type\" [ \"dielectric\" ] \"float eta\" [ 1.7 ]\n"
		"Material \"mix\" \"string materials\" [ \"a\" \"b\" ] \"float amount\" [ 0.75 ]\n"
		+ std::string(kQuad));
	hit_record rec;
	ASSERT_TRUE(b.world->hit(ray(point3(0.5, 0.5, -5), vec3(0, 0, 1)),
							 interval(0.001, infinity), rec));
	auto *mix = dynamic_cast<mix_material *>(rec.mat.get());
	ASSERT_NE(mix, nullptr)
		<< "a pbrt mix material must build the real mix_material class, not "
		   "silently fall back to lambertian";
	EXPECT_NE(dynamic_cast<conductor *>(mix->get_mat_a().get()), nullptr)
		<< "sub-material 'a' (conductor) must build the real conductor class";
	EXPECT_NE(dynamic_cast<dielectric *>(mix->get_mat_b().get()), nullptr)
		<< "sub-material 'b' (dielectric) must build the real dielectric class";
	EXPECT_DOUBLE_EQ(mix->get_weight()->value(0, 0, point3(0, 0, 0)).x(), 0.75);
}

TEST(PbrtCpuBuildTest, MalformedMixFallsBackToLambertianLikeAnyOtherUnsupportedMaterial) {
	// flatten() already downgrades an unresolvable mix to MaterialKind::
	// Unsupported (pinned in pbrt_flatten_tests.cpp) - this just confirms
	// the builder's existing Unsupported->lambertian fallback still applies,
	// rather than the builder crashing or constructing a mix_material with
	// dangling sub-material indices.
	const pbrt_cpu::BuildResult b = buildFrom(
		"Material \"mix\" \"string materials\" [ \"nope\" \"alsonope\" ]\n"
		+ std::string(kQuad));
	hit_record rec;
	ASSERT_TRUE(b.world->hit(ray(point3(0.5, 0.5, -5), vec3(0, 0, 1)),
							 interval(0.001, infinity), rec));
	EXPECT_NE(dynamic_cast<lambertian *>(rec.mat.get()), nullptr);
}
