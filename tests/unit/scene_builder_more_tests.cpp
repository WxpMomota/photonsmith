/**
 * @file scene_builder_more_tests.cpp
 * @brief Scene Builder tests, second half: volumes (fog, smoke, clouds), more materials, instancing, dots, 3D text.
 * Helpers are in scene_builder_test_helpers.h; the first half is scene_builder_tests.cpp.
 */

#include "scene_builder_test_helpers.h"

// ---------------------------------------------------------------------------------------------------------------------------------
// Volumes: fog, smoke and clouds (Volume in scene_model.h)
// ---------------------------------------------------------------------------------------------------------------------------------
using scene_doc::VolumeKind;
using scene_doc::makeVolume;

namespace {

// A camera looking down -Z at a unit-radius volume at the origin through a very narrow window, under a uniform white sky of radiance 1.
Document volumeThroughASky(const Object& volume, double fov = 1.0) {
	Document d;
	d.camera.position = {0, 0, 10};
	d.camera.target = {0, 0, 0};
	d.camera.fov = fov;
	d.objects.push_back(volume);
	Light sky;
	sky.kind = LightKind::Infinite;
	sky.color = {1, 1, 1};
	sky.intensity = 1.0;
	d.lights.push_back(sky);
	return d;
}

}  // namespace

TEST(SceneVolumeTest, AVolumeRoundTripsAndASceneWithoutOneWritesNothingNew) {
	Document d = makeStarterScene();
	EXPECT_EQ(toJson(d).find("volume"), std::string::npos) << "a scene with no volumes writes exactly what it always did";
	Object cloud = makeVolume(VolumeKind::Cloud, "Puff");
	cloud.volume.wispiness = 0.7;
	cloud.volume.frequency = 5.5;
	cloud.volume.anisotropy = -0.25;
	cloud.volume.color = {0.9, 0.95, 1.0};
	d.objects.push_back(cloud);
	d.objects.push_back(makeVolume(VolumeKind::Smoke, "Smoke"));
	Document back;
	std::string error;
	ASSERT_TRUE(fromJson(toJson(d), back, error)) << error;
	EXPECT_EQ(toJson(back), toJson(d));
	ASSERT_EQ(back.objects.size(), d.objects.size());
	EXPECT_EQ(back.objects[back.objects.size() - 2].volume.kind, VolumeKind::Cloud);
	EXPECT_DOUBLE_EQ(back.objects[back.objects.size() - 2].volume.frequency, 5.5);
	EXPECT_DOUBLE_EQ(back.objects.back().volume.density, 1.6);
	EXPECT_EQ(back.objects.front().volume.kind, VolumeKind::None);
}

TEST(SceneVolumeTest, FogSmokeAndCloudAreWrittenAsMediaTheRendererLoads) {
	Document d = makeStarterScene();
	d.objects.push_back(makeVolume(VolumeKind::Fog, "Mist"));
	d.objects.push_back(makeVolume(VolumeKind::Smoke, "Smoke"));
	d.objects.push_back(makeVolume(VolumeKind::Cloud, "Puff"));
	Object tube = makeVolume(VolumeKind::Fog, "Fog column");
	tube.shape = ShapeKind::Cylinder;
	tube.radius = 0.8;
	tube.height = 3.0;
	d.objects.push_back(tube);
	ASSERT_FALSE(hasErrors(validate(d))) << validate(d).front().message;
	const std::string text = toPbrt(d);
	EXPECT_NE(text.find("MakeNamedMedium \"volume-1\" \"string type\" [ \"homogeneous\" ]"), std::string::npos);
	EXPECT_NE(text.find("MakeNamedMedium \"volume-3\" \"string type\" [ \"cloud\" ]"), std::string::npos);
	EXPECT_NE(text.find("MediumInterface \"volume-2\" \"\""), std::string::npos);
	EXPECT_NE(text.find("Material \"interface\""), std::string::npos);
	// sigma_s = density * colour and sigma_a = density * (1 - colour): the smoke (density 1.6, colour 0.30 0.30 0.32) absorbs 70% of what it takes from the light.
	EXPECT_NE(text.find("\"rgb sigma_a\" [ 1.12 1.12 1.088 ]"), std::string::npos) << text;
	EXPECT_NE(text.find("\"rgb sigma_s\" [ 0.48 0.48 0.512 ]"), std::string::npos);
	const pbrt_load::LoadResult r = loadText(text, "volumes");
	ASSERT_TRUE(r.ok) << r.error;
	// It survives the round trip through the file too.
	Document back;
	std::string error;
	ASSERT_TRUE(fromPbrt(text, back, error)) << error;
	EXPECT_EQ(toJson(back), toJson(d));
}

TEST(SceneVolumeTest, AVolumeThatTheRendererCannotFillIsAnErrorAndItsLightIsIgnored) {
	Document d = makeStarterScene();
	Object box = makeVolume(VolumeKind::Fog, "Box of fog");
	box.shape = ShapeKind::Box;
	d.objects.push_back(box);
	auto problems = validate(d);
	ASSERT_TRUE(hasErrors(problems));
	EXPECT_NE(problems.back().message.find("sphere"), std::string::npos);
	d.objects.back() = makeVolume(VolumeKind::Cloud, "Cloud tube");
	d.objects.back().shape = ShapeKind::Cylinder;
	EXPECT_TRUE(hasErrors(validate(d))) << "a cloud only fills a sphere";
	d.objects.back() = makeVolume(VolumeKind::Fog, "Fog");
	d.objects.back().volume.density = 0.0;
	EXPECT_TRUE(hasErrors(validate(d)));
	d.objects.back() = makeVolume(VolumeKind::Fog, "Fog");
	d.objects.back().volume.color = {1.5, 1.0, 1.0};
	EXPECT_TRUE(hasErrors(validate(d)));
	d.objects.back() = makeVolume(VolumeKind::Fog, "Fog");
	d.objects.back().volume.anisotropy = 1.0;
	EXPECT_TRUE(hasErrors(validate(d)));
	d.objects.back() = makeVolume(VolumeKind::Fog, "Fog");
	d.objects.back().emissive = true;   // not an error, a note; and it is no light
	problems = validate(d);
	EXPECT_FALSE(hasErrors(problems));
	Document dark;
	dark.objects.push_back(d.objects.back());
	bool noLight = false;
	for (const Problem& p : validate(dark)) noLight = noLight || p.message.find("no light") != std::string::npos;
	EXPECT_TRUE(noLight) << "an emissive volume does not count as a light";
}

// A pure absorber (scattering colour 0) of density s and radius 1 seen through a window so narrow that every ray crosses it near the middle, chord 2: it passes exp(-2 s) of the
// sky's radiance 1.
TEST(SceneVolumeRenderTest, AnAbsorbingVolumeTransmitsTheBeerLambertFraction) {
	const std::string launcher = findLauncher();
	if (launcher.empty()) GTEST_SKIP() << "ray_tracer.exe was not found next to the tests";
	for (double density : {0.5, 1.0}) {
		Object v = makeVolume(VolumeKind::Smoke, "Absorber");
		v.position = {0, 0, 0};
		v.radius = 1.0;
		v.volume.density = density;
		v.volume.color = {0, 0, 0};
		v.volume.anisotropy = 0.0;
		const Document d = volumeThroughASky(v);
		ASSERT_FALSE(hasErrors(validate(d)));
		std::vector<float> rgb;
		int w = 0, h = 0;
		ASSERT_TRUE(renderWithLauncher(launcher, toPbrt(d), "absorber", 24, 192, 6, rgb, w, h));
		const double want = std::exp(-2.0 * density);
		for (int c = 0; c < 3; ++c) EXPECT_NEAR(channelMean(rgb, c), want, 0.05 * want) << "density " << density << " channel " << c;
	}
}

// A volume that only scatters (white) inside a uniform sky of radiance 1 cannot change the picture: every ray leaves with radiance 1 however often it scatters (a furnace). It
// checks the whole scattering path, for the fog and for the cloud (whose density varies, so it is sampled by delta tracking).
TEST(SceneVolumeRenderTest, AWhiteVolumeInAUniformSkyIsInvisible) {
	const std::string launcher = findLauncher();
	if (launcher.empty()) GTEST_SKIP() << "ray_tracer.exe was not found next to the tests";
	for (VolumeKind kind : {VolumeKind::Fog, VolumeKind::Cloud}) {
		Object v = makeVolume(kind, "White");
		v.position = {0, 0, 0};
		v.radius = 1.0;
		v.volume.density = kind == VolumeKind::Cloud ? 8.0 : 2.0;
		v.volume.color = {1, 1, 1};
		const Document d = volumeThroughASky(v, 14.0);   // the whole sphere in view this time
		ASSERT_FALSE(hasErrors(validate(d)));
		std::vector<float> rgb;
		int w = 0, h = 0;
		ASSERT_TRUE(renderWithLauncher(launcher, toPbrt(d), "furnace_volume", 32, 128, 100, rgb, w, h));
		for (int c = 0; c < 3; ++c) EXPECT_NEAR(channelMean(rgb, c), 1.0, 0.03) << toString(kind) << " channel " << c;
	}
}

// RT_VOLUMES_PBRT=<file> writes a scene with one of each volume, to look at the starting values.
TEST(SceneVolumeTest, WritesADemoSceneWhenAsked) {
	const char* out = std::getenv("RT_VOLUMES_PBRT");
	if (!out) GTEST_SKIP() << "set RT_VOLUMES_PBRT=<file> to write the demo scene";
	Document d = makeStarterScene();
	d.objects.resize(1);   // keep the floor
	d.objects[0].size = {30.0, 1.0, 30.0};
	d.objects.push_back(makeAreaLightPanel("Ceiling light"));
	d.objects.back().position = {0, 9.0, 0};
	d.objects.back().size = {4, 1, 4};
	Object fog = makeVolume(VolumeKind::Fog, "Fog");
	fog.position = {-4.5, 2.0, 0};
	Object smoke = makeVolume(VolumeKind::Smoke, "Smoke");
	smoke.position = {0, 1.4, 0};
	Object cloud = makeVolume(VolumeKind::Cloud, "Cloud");
	cloud.position = {5.0, 3.0, 0};
	d.objects.push_back(fog);
	d.objects.push_back(smoke);
	d.objects.push_back(cloud);
	Object ball = makeObject(ShapeKind::Sphere, "Marker");
	ball.radius = 0.4;
	ball.position = {-4.5, 0.4, 0};
	ball.material.color = {0.8, 0.2, 0.1};
	d.objects.push_back(ball);
	ball.position = {0, 0.4, 0};
	d.objects.push_back(ball);
	ball.position = {5.0, 0.4, 0};
	d.objects.push_back(ball);
	d.camera.position = {0.0, 4.5, 15.0};
	d.camera.target = {0.0, 2.0, 0.0};
	d.camera.fov = 45;
	d.render = {640, 360, 128, 12};
	std::ofstream(out, std::ios::binary) << toPbrt(d);
}

// ---------------------------------------------------------------------------------------------------------------------------------
// A medium fills the shape it is attached to, not the box its density is declared in (src/shared/medium_clip.h)
// ---------------------------------------------------------------------------------------------------------------------------------
#include "../../src/shared/medium_clip.h"

TEST(MediumSphereClipTest, ShortensASegmentToTheChordInsideTheSphere) {
	MediumSphereClip clip;
	clip.set(0, 0, 0, 1.0);
	double t0 = 0.0, t1 = 100.0;
	ASSERT_TRUE(clip.apply(0, 0, -5, 0, 0, 1, t0, t1));   // straight through the middle: from z = -1 to 1
	EXPECT_NEAR(t0, 4.0, 1e-12);
	EXPECT_NEAR(t1, 6.0, 1e-12);
	t0 = 5.0;   // a segment that starts inside the sphere keeps its start
	t1 = 100.0;
	ASSERT_TRUE(clip.apply(0, 0, -5, 0, 0, 1, t0, t1));
	EXPECT_NEAR(t0, 5.0, 1e-12);
	EXPECT_NEAR(t1, 6.0, 1e-12);
	t0 = 0.0;   // one that ends before the sphere, or misses it, leaves nothing
	t1 = 3.0;
	EXPECT_FALSE(clip.apply(0, 0, -5, 0, 0, 1, t0, t1));
	t0 = 0.0;
	t1 = 100.0;
	EXPECT_FALSE(clip.apply(1.5, 0, -5, 0, 0, 1, t0, t1));
	t0 = 0.0;
	t1 = 100.0;
	ASSERT_TRUE(clip.apply(0.6, 0, -5, 0, 0, 1, t0, t1));   // off to one side: a chord of 2 * sqrt(1 - 0.36)
	EXPECT_NEAR(t1 - t0, 2.0 * std::sqrt(1.0 - 0.36), 1e-12);
	MediumSphereClip none;
	t0 = 1.0;
	t1 = 2.0;
	EXPECT_TRUE(none.apply(0, 0, 0, 1, 0, 0, t0, t1));   // no sphere: left alone
	EXPECT_EQ(t0, 1.0);
	EXPECT_EQ(t1, 2.0);
}

// A medium whose density grid is declared in a box eight units across, on an interface sphere of radius 1, must look like the same scattering fog filling just the sphere.
// (Before the clip the CPU filled the whole box.)
TEST(SceneVolumeRenderTest, AGridMediumInABoxBiggerThanItsSphereIsCutRoundBySphere) {
	const std::string launcher = findLauncher();
	if (launcher.empty()) GTEST_SKIP() << "ray_tracer.exe was not found next to the tests";
	const std::string head =
	    "LookAt 0 0 10  0 0 0  0 1 0\nCamera \"perspective\" \"float fov\" [ 14 ]\nFilm \"rgb\" \"integer xresolution\" [ 32 ] \"integer yresolution\" [ 32 ]\n"
	    "Sampler \"halton\" \"integer pixelsamples\" [ 64 ]\nIntegrator \"volpath\" \"integer maxdepth\" [ 40 ]\nWorldBegin\n"
	    "LightSource \"infinite\" \"rgb L\" [ 0.2 0.3 0.5 ]\nLightSource \"distant\" \"point3 from\" [ 1 2 3 ] \"point3 to\" [ 0 0 0 ] \"rgb L\" [ 3 3 3 ]\n";
	const std::string sphere = "Material \"interface\"\nMediumInterface \"m\" \"\"\nShape \"sphere\" \"float radius\" [ 1 ]\n";
	const std::string fog = "MakeNamedMedium \"m\" \"string type\" [ \"homogeneous\" ] \"rgb sigma_a\" [ 0 0 0 ] \"rgb sigma_s\" [ 0.8 0.8 0.8 ] \"float g\" [ 0 ]\n";
	const std::string grid =
	    "AttributeBegin\n  Translate -4 -4 -4\n  Scale 8 8 8\n  MakeNamedMedium \"m\" \"string type\" [ \"uniformgrid\" ] \"integer nx\" [ 1 ] \"integer ny\" [ 1 ] \"integer nz\" [ 1 ] "
	    "\"float density\" [ 1 ] \"rgb sigma_s\" [ 0.8 0.8 0.8 ] \"float g\" [ 0 ]\nAttributeEnd\n";
	std::vector<float> a, b;
	int w = 0, h = 0;
	ASSERT_TRUE(renderWithLauncher(launcher, head + fog + sphere, "fog_sphere", 32, 64, 40, a, w, h));
	ASSERT_TRUE(renderWithLauncher(launcher, head + grid + sphere, "grid_sphere", 32, 64, 40, b, w, h));
	for (int c = 0; c < 3; ++c) EXPECT_NEAR(channelMean(b, c), channelMean(a, c), 0.04 * channelMean(a, c)) << "channel " << c;
}

// ---------------------------------------------------------------------------------------------------------------------------------
// More materials: subsurface, marble and noise patterns, bump and normal maps
// ---------------------------------------------------------------------------------------------------------------------------------
using scene_doc::BumpKind;
using scene_doc::Pattern;

TEST(SceneMaterialsMoreTest, TheNewFieldsRoundTripAndASceneWithoutThemWritesNothingNew) {
	Document plain = makeStarterScene();
	const std::string text = toJson(plain);
	for (const char* key : {"pattern", "scatterDistance", "bumpFile", "bumpKind"}) EXPECT_EQ(text.find(key), std::string::npos) << key;
	Document d = plain;
	Object skin = makeObject(ShapeKind::Sphere, "Skin ball");
	scene_doc::applyMaterialPreset(skin.material, "skin");
	skin.material.scatterDistance = {0.2, 0.1, 0.05};
	d.objects.push_back(skin);
	Object stone = makeObject(ShapeKind::Box, "Stone");
	stone.material.pattern = Pattern::Marble;
	stone.material.patternScale = 7.5;
	stone.material.patternVariation = 0.45;
	stone.material.bumpFile = "C:\\maps\\bumps.png";
	stone.material.bumpKind = BumpKind::Normal;
	stone.material.bumpStrength = 0.2;
	d.objects.push_back(stone);
	Object cloudy = makeObject(ShapeKind::Quad, "Cloudy");
	cloudy.material.kind = MaterialKind::CoatedDiffuse;
	cloudy.material.pattern = Pattern::Noise;
	d.objects.push_back(cloudy);
	Document back;
	std::string error;
	ASSERT_TRUE(fromJson(toJson(d), back, error)) << error;
	EXPECT_EQ(toJson(back), toJson(d));
	const scene_doc::Material& m = back.objects[back.objects.size() - 2].material;
	EXPECT_EQ(m.pattern, Pattern::Marble);
	EXPECT_DOUBLE_EQ(m.patternScale, 7.5);
	EXPECT_EQ(m.bumpKind, BumpKind::Normal);
	EXPECT_EQ(m.bumpFile, "C:\\maps\\bumps.png");
	EXPECT_EQ(back.objects[back.objects.size() - 3].material.kind, MaterialKind::Subsurface);
	EXPECT_DOUBLE_EQ(back.objects[back.objects.size() - 3].material.scatterDistance.r, 0.2);
}

TEST(SceneMaterialsMoreTest, SubsurfacePatternsAndBumpMapsAreWrittenAsPbrtTheRendererLoads) {
	Document d = makeStarterScene();
	Object skin = makeObject(ShapeKind::Sphere, "Skin");
	scene_doc::applyMaterialPreset(skin.material, "skin");
	d.objects.push_back(skin);
	Object marble = makeObject(ShapeKind::Box, "Marble");
	marble.material.pattern = Pattern::Marble;
	marble.material.patternScale = 6.0;
	d.objects.push_back(marble);
	Object noise = makeObject(ShapeKind::Quad, "Noise");
	noise.material.kind = MaterialKind::CoatedDiffuse;
	noise.material.pattern = Pattern::Noise;
	d.objects.push_back(noise);
	// Two picture files that exist, so the loader has them.
	const std::string heights = std::filesystem::absolute("scene_builder_heights.bmp").string(), normals = std::filesystem::absolute("scene_builder_normals.bmp").string();
	{
		std::ofstream a(heights, std::ios::binary), b(normals, std::ios::binary);
		a << bmpFile(2, 2, std::vector<unsigned char>(12, 100));
		b << bmpFile(2, 2, std::vector<unsigned char>(12, 128));
	}
	Object bumpy = makeObject(ShapeKind::Quad, "Bumpy");
	bumpy.material.bumpFile = heights;
	bumpy.material.bumpStrength = 0.07;
	d.objects.push_back(bumpy);
	Object tilted = makeObject(ShapeKind::Box, "Tilted");
	tilted.material.kind = MaterialKind::Conductor;
	tilted.material.bumpFile = normals;
	tilted.material.bumpKind = BumpKind::Normal;
	d.objects.push_back(tilted);
	ASSERT_FALSE(hasErrors(validate(d)));
	const std::string text = toPbrt(d);
	EXPECT_NE(text.find("Material \"subsurface\" \"rgb reflectance\" [ 0.8 0.58 0.48 ] \"rgb mfp\" [ 0.14 0.055 0.036 ] \"float eta\" [ 1.4 ] \"float roughness\" [ 0.35 ]"), std::string::npos) << text;
	EXPECT_NE(text.find("\"spectrum\" \"marble\" \"float scale\" [ 6 ]"), std::string::npos);
	EXPECT_NE(text.find("\"spectrum\" \"fbm\""), std::string::npos);
	EXPECT_NE(text.find("\"float\" \"imagemap\""), std::string::npos);
	EXPECT_NE(text.find("\"float\" \"scale\" \"texture tex\""), std::string::npos);
	EXPECT_NE(text.find("\"float scale\" [ 0.07 ]"), std::string::npos);
	EXPECT_NE(text.find("\"texture displacement\""), std::string::npos);
	const pbrt_load::LoadResult r = loadText(text, "materials_more");
	EXPECT_TRUE(r.ok) << r.error;
	std::remove(heights.c_str());
	std::remove(normals.c_str());
	Document back;
	std::string error;
	ASSERT_TRUE(fromPbrt(text, back, error)) << error;
	EXPECT_EQ(toJson(back), toJson(d));
}

TEST(SceneMaterialsMoreTest, ProblemsWithTheNewMaterialFieldsAreReported) {
	Document d = makeStarterScene();
	Object o = makeObject(ShapeKind::Sphere, "Ball");
	o.material.kind = MaterialKind::Subsurface;
	o.material.scatterDistance = {0.1, 0.0, 0.1};
	d.objects.push_back(o);
	EXPECT_TRUE(hasErrors(validate(d))) << "a distance of zero in a colour";
	d.objects.back().material.scatterDistance = {0.1, 0.1, 0.1};
	EXPECT_FALSE(hasErrors(validate(d)));
	d.objects.back().material = scene_doc::Material{};
	d.objects.back().material.pattern = Pattern::Marble;
	d.objects.back().material.patternScale = 0.0;
	EXPECT_TRUE(hasErrors(validate(d)));
	d.objects.back().material.patternScale = 4.0;
	d.objects.back().material.imageFile = "relative.png";
	bool replaced = false;
	for (const Problem& p : validate(d)) replaced = replaced || p.message.find("replaces the marble") != std::string::npos;
	EXPECT_TRUE(replaced) << "a picture replaces the pattern";
	d.objects.back().material.imageFile.clear();
	d.objects.back().material.bumpFile = "bumps.png";
	const auto heightMapWarned = [&]() {
		bool warned = false;
		for (const Problem& p : validate(d)) warned = warned || p.message.find("does not draw a height map") != std::string::npos || p.message.find("do not draw a height map") != std::string::npos;
		return warned;
	};
	EXPECT_TRUE(heightMapWarned()) << "OptiX does not draw one on a sphere (Metal does)";
	d.objects.back().shape = ShapeKind::Disk;
	EXPECT_TRUE(heightMapWarned()) << "OptiX does not draw one on a disk (Metal does)";
	d.objects.back().shape = ShapeKind::Cylinder;
	EXPECT_TRUE(heightMapWarned());
	d.objects.back().shape = ShapeKind::Cone;
	EXPECT_FALSE(heightMapWarned()) << "a cone is drawn as triangles: both GPU renderers apply it";
	d.objects.back().shape = ShapeKind::Box;
	EXPECT_FALSE(heightMapWarned()) << "a box is made of triangles: the GPU applies it";
	d.objects.back().material.kind = MaterialKind::Dielectric;
	bool onlyFor = false;
	for (const Problem& p : validate(d)) onlyFor = onlyFor || p.message.find("only applies to matte, glossy paint and metal") != std::string::npos;
	EXPECT_TRUE(onlyFor);
}

TEST(SceneMaterialsMoreTest, ApplyingAPresetKeepsTheBumpSlot) {
	scene_doc::Material m;
	m.bumpFile = "bumps.png";
	m.bumpKind = BumpKind::Normal;
	m.bumpStrength = 0.3;
	m.pattern = Pattern::Marble;
	ASSERT_TRUE(scene_doc::applyMaterialPreset(m, "gold"));
	EXPECT_EQ(m.kind, MaterialKind::Conductor);
	EXPECT_EQ(m.bumpFile, "bumps.png");
	EXPECT_EQ(m.bumpKind, BumpKind::Normal);
	EXPECT_DOUBLE_EQ(m.bumpStrength, 0.3);
	EXPECT_EQ(m.pattern, Pattern::None) << "the colour is the preset's";
}

// A diffuse plate of albedo 1 lit from straight above by a distant light of irradiance pi reads 1. A normal map whose every texel tilts the normal by 30 degrees leaves the light
// 30 degrees off the shading normal, so the plate reads cos(30 deg): the writer's texture binding really reaches the renderer's normal mapping.
TEST(SceneMaterialsMoreTest, ANormalMapTiltsTheShadingByTheAngleItEncodes) {
	const std::string launcher = findLauncher();
	if (launcher.empty()) GTEST_SKIP() << "ray_tracer.exe was not found next to the tests";
	// A tangent-space normal (sin30, 0, cos30) as RGB: ((x+1)/2, (y+1)/2, (z+1)/2) * 255 = (191, 128, 238).
	const std::string normals = std::filesystem::absolute("scene_builder_tilt_normals.bmp").string();
	{
		std::ofstream out(normals, std::ios::binary);
		std::vector<unsigned char> px;
		for (int i = 0; i < 4; ++i) { px.push_back(191); px.push_back(128); px.push_back(238); }
		out << bmpFile(2, 2, px);
	}
	const auto plate = [&](bool tilted) {
		Document d;
		d.camera.position = {0, 10, 0.001};
		d.camera.target = {0, 0, 0};
		d.camera.fov = 4;
		Object quad = makeObject(ShapeKind::Quad, "Plate");
		quad.position = {0, 0, 0};
		quad.size = {20, 1, 20};
		quad.material.color = {1, 1, 1};
		if (tilted) {
			quad.material.bumpFile = normals;
			quad.material.bumpKind = BumpKind::Normal;
		}
		d.objects.push_back(quad);
		Light sun;
		sun.kind = LightKind::Distant;
		sun.position = {0, 10, 0};
		sun.target = {0, 0, 0};
		sun.color = {1, 1, 1};
		sun.intensity = 3.14159265;
		d.lights.push_back(sun);
		return d;
	};
	std::vector<float> flat, tilt;
	int w = 0, h = 0;
	const bool ok = renderWithLauncher(launcher, toPbrt(plate(false)), "plate_flat", 24, 64, 1, flat, w, h) && renderWithLauncher(launcher, toPbrt(plate(true)), "plate_tilt", 24, 64, 1, tilt, w, h);
	std::remove(normals.c_str());
	ASSERT_TRUE(ok);
	const double base = channelMean(flat, 1);
	EXPECT_NEAR(base, 1.0, 0.03) << "the flat white plate under irradiance pi";
	EXPECT_NEAR(channelMean(tilt, 1) / base, std::cos(30.0 * 3.14159265358979 / 180.0), 0.04);
}

// RT_PRESET_SCENES_DIR=<folder> writes one scene per material preset (a sphere in a small studio), which scripts/make_material_thumbnails.py renders into the pictures the
// Scene Builder's preset list shows.
TEST(SceneMaterialsMoreTest, WritesOneSceneForEveryPresetWhenAsked) {
	const char* dir = std::getenv("RT_PRESET_SCENES_DIR");
	if (!dir) GTEST_SKIP() << "set RT_PRESET_SCENES_DIR=<folder> to write the preset scenes";
	const auto write = [&](const std::string& file, const scene_doc::Material& material) {
		Document d;
		d.camera.position = {0, 0.55, 4.6};
		d.camera.target = {0, 0.0, 0};
		d.camera.fov = 28;
		d.render = {64, 64, 96, 12};
		Object ball = makeObject(ShapeKind::Sphere, file);
		ball.position = {0, 0, 0};
		ball.radius = 1.0;
		ball.material = material;
		d.objects.push_back(ball);
		Object floorObj = makeObject(ShapeKind::Quad, "Floor");
		floorObj.position = {0, -1.0, 0};
		floorObj.size = {30, 1, 30};
		floorObj.material.color = {0.55, 0.55, 0.55};
		d.objects.push_back(floorObj);
		Light sky;
		sky.kind = LightKind::Infinite;
		sky.color = {0.62, 0.72, 0.95};
		sky.intensity = 0.7;
		d.lights.push_back(sky);
		Light sun;
		sun.kind = LightKind::Distant;
		sun.position = {3, 5, 4};
		sun.target = {0, 0, 0};
		sun.color = {1.0, 0.96, 0.9};
		sun.intensity = 3.0;
		d.lights.push_back(sun);
		std::ofstream(std::string(dir) + "/" + file + ".pbrt", std::ios::binary) << toPbrt(d);
	};
	for (const scene_doc::MaterialPreset& preset : scene_doc::materialPresets()) {
		scene_doc::Material m;
		scene_doc::applyMaterialPreset(m, preset.id);
		write(preset.id, m);
	}
	for (const scene_doc::MixPreset& preset : scene_doc::mixPresets()) write(std::string("mix-") + preset.id, preset.material);   // (the pictures of the mix list)
}

// RT_COMBO_SCENES_DIR=<folder> writes one small scene for every shape the Scene Builder has under every material treatment it offers (a plain colour, each material type, a mix, an
// emitter, a picture, a checker, a marble, a height map and a normal map, a mix whose first material has a picture or a bump), named <shape>__<treatment>.pbrt, for
// scripts/combo_sweep.py to render on the CPU and both OptiX backends and compare: a combination one renderer cannot draw, or draws differently, shows there.
TEST(SceneCombosTest, WritesOneSceneForEveryShapeAndMaterialTreatmentWhenAsked) {
	const char* dirEnv = std::getenv("RT_COMBO_SCENES_DIR");
	if (!dirEnv) GTEST_SKIP() << "set RT_COMBO_SCENES_DIR=<folder> to write the combination scenes";
	const std::string dir = std::filesystem::absolute(dirEnv).string();
	const auto writeFile = [&](const std::string& name, const std::string& bytes) { std::ofstream(dir + "/" + name, std::ios::binary) << bytes; return dir + "/" + name; };
	std::vector<unsigned char> picture, ramp, tilt;
	for (int i = 0; i < 16; ++i) {   // a 4x4 picture of four colours
		const int x = i % 4, y = i / 4;
		const bool a = ((x + y) % 2) == 0;
		for (unsigned char v : (a ? std::vector<unsigned char>{230, 60, 40} : std::vector<unsigned char>{40, 90, 230})) picture.push_back(v);
	}
	for (int y = 0; y < 4; ++y)
		for (int x = 0; x < 16; ++x) for (int c = 0; c < 3; ++c) ramp.push_back(static_cast<unsigned char>(255 * x / 15));
	for (int i = 0; i < 4; ++i) { tilt.push_back(238); tilt.push_back(128); tilt.push_back(191); }   // a normal tilted 60 degrees about +x
	// PNG, not BMP: pbrt-v4 reads a PNG with the encoding it is asked for (a normal map linear) but decodes every other format (BMP, JPG, TGA) as sRGB whatever it is asked, which
	// would make its picture of a normal map differ from ours for a reason that is not ours (docs/PBRT_SUPPORT.md).
	const auto writePng = [&](const std::string& name, int w, int h, const std::vector<unsigned char>& rgb) {
		stbi_write_png((dir + "/" + name).c_str(), w, h, 3, rgb.data(), w * 3);
		return dir + "/" + name;
	};
	const std::string pictureFile = writePng("combo_picture.png", 4, 4, picture), rampFile = writePng("combo_ramp.png", 16, 4, ramp), tiltFile = writePng("combo_tilt.png", 2, 2, tilt);
	struct Treatment {
		const char* name;
		std::function<void(Object&)> apply;
	};
	const auto kind = [](MaterialKind k, scene_doc::Rgb c, double roughness = 0.0, double ior = 1.5) {
		return [=](Object& o) {
			o.material.kind = k;
			o.material.color = c;
			o.material.roughness = roughness;
			o.material.ior = ior;
		};
	};
	std::vector<Treatment> treatments = {
	    {"diffuse", kind(MaterialKind::Diffuse, {0.7, 0.5, 0.3})},
	    {"conductor", kind(MaterialKind::Conductor, {0.95, 0.64, 0.54}, 0.2)},
	    {"glass", kind(MaterialKind::Dielectric, {0.8, 0.8, 0.8}, 0.0, 1.5)},
	    {"coated", kind(MaterialKind::CoatedDiffuse, {0.7, 0.1, 0.1}, 0.05, 1.5)},
	    {"transmission", [](Object& o) { o.material.kind = MaterialKind::DiffuseTransmission; o.material.color = {0.5, 0.6, 0.3}; o.material.transmittance = {0.3, 0.4, 0.1}; }},
	    {"subsurface", [](Object& o) { scene_doc::applyMaterialPreset(o.material, "skin"); }},
	    {"mix", [](Object& o) { scene_doc::applyMixPreset(o.material, "rusty-metal"); }},
	    {"emitter", [](Object& o) { o.emissive = true; o.emission = {1.0, 0.8, 0.5}; o.emissionStrength = 3.0; }},
	    {"picture", [=](Object& o) { o.material.imageFile = pictureFile; o.material.uvScaleU = 3.0; o.material.uvScaleV = 2.0; o.material.uvOffsetU = 0.25; }},
	    {"checker", [](Object& o) { o.material.checker = true; o.material.color = {0.9, 0.9, 0.9}; o.material.color2 = {0.1, 0.2, 0.6}; o.material.checkerCount = 4; o.material.uvScaleU = 2.0; o.material.uvOffsetU = 0.3; }},
	    {"marble", [](Object& o) { o.material.pattern = Pattern::Marble; }},
	    {"heightmap", [=](Object& o) { o.material.color = {0.8, 0.8, 0.8}; o.material.bumpFile = rampFile; o.material.bumpKind = BumpKind::Height; o.material.bumpStrength = 3.0; o.material.uvScaleU = 6.0; }},
	    {"normalmap", [=](Object& o) { o.material.color = {0.8, 0.8, 0.8}; o.material.bumpFile = tiltFile; o.material.bumpKind = BumpKind::Normal; }},
	    {"mix_picture", [=](Object& o) { scene_doc::applyMixPreset(o.material, "chipped-paint"); o.material.imageFile = pictureFile; o.material.uvScaleU = 2.0; }},
	    {"mix_normalmap", [=](Object& o) { scene_doc::applyMixPreset(o.material, "worn-leather"); o.material.bumpFile = tiltFile; o.material.bumpKind = BumpKind::Normal; }},
	};
	int written = 0;
	for (ShapeKind shape : scene_doc::allShapeKinds()) {
		if (shape == ShapeKind::Mesh || shape == ShapeKind::Cyclorama) continue;   // (a mesh needs a file; a cyclorama is a backdrop bigger than the frame)
		for (const Treatment& t : treatments) {
			Document d;
			d.camera.position = {3.2, 2.4, 4.8};
			d.camera.target = {0, 0.8, 0};
			d.camera.fov = 40;
			d.render = {64, 64, 48, 8};
			Object floorObj = makeObject(ShapeKind::Quad, "Floor");
			floorObj.material.color = {0.55, 0.55, 0.55};
			d.objects.push_back(floorObj);
			Object o = makeObject(shape, "Subject");
			if (shape == ShapeKind::Quad) { o.size = {2, 1, 2}; o.position = {0, 1.0, 0}; o.rotation = {-70, 0, 0}; }
			if (shape == ShapeKind::Terrain) { o.size = {4, 1, 4}; o.position = {0, 0.5, 0}; }
			t.apply(o);
			d.objects.push_back(o);
			Light sky;
			sky.kind = LightKind::Infinite;
			sky.color = {0.62, 0.72, 0.95};
			sky.intensity = 0.7;
			d.lights.push_back(sky);
			Light sun;
			sun.kind = LightKind::Distant;
			sun.position = {3, 5, 4};
			sun.target = {0, 0, 0};
			sun.color = {1.0, 0.96, 0.9};
			sun.intensity = 3.0;
			d.lights.push_back(sun);
			writeFile(std::string(scene_doc::toString(shape)) + "__" + t.name + ".pbrt", toPbrt(d));
			++written;
		}
	}
	// Copies of the heavy shapes, which the writer places as instances of one template (scene_instancing.h): three of the subject, moved, turned and one scaled, under each treatment
	// an instance can take, named inst_<shape>__<treatment>.pbrt. The renderers draw them as they draw the plain copies, and real pbrt-v4 reads them (pbrt_compare.py).
	for (ShapeKind shape : {ShapeKind::Cone, ShapeKind::Torus, ShapeKind::Capsule, ShapeKind::Dome, ShapeKind::Tube, ShapeKind::RoundedBox, ShapeKind::Terrain}) {
		for (const Treatment& t : treatments) {
			if (std::string(t.name) == "emitter" || std::string(t.name) == "marble") continue;   // (an emitter and a pattern are not instanced)
			Document d;
			d.camera.position = {3.6, 2.8, 5.4};
			d.camera.target = {0, 0.7, 0};
			d.camera.fov = 44;
			d.render = {64, 64, 48, 8};
			Object floorObj = makeObject(ShapeKind::Quad, "Floor");
			floorObj.material.color = {0.55, 0.55, 0.55};
			d.objects.push_back(floorObj);
			Object o = makeObject(shape, "Subject");
			if (shape == ShapeKind::Terrain) { o.size = {2, 0.6, 2}; o.position = {0, 0.3, 0}; }
			t.apply(o);
			const double scales[3] = {1.0, 0.7, 1.0};
			const scene_doc::Float3 where[3] = {{-1.3, 0, 0.3}, {1.4, 0, -0.5}, {0.1, 0, 1.5}};
			const double turn[3] = {0.0, 40.0, 90.0};
			for (int i = 0; i < 3; ++i) {
				Object c = o;
				c.name = "Copy " + std::to_string(i + 1);
				scene_doc::scaleObject(c, scales[i]);
				c.position = {where[i].x, o.position.y * scales[i], where[i].z};
				c.rotation = {0.0, turn[i], 0.0};
				d.objects.push_back(c);
			}
			Light sky;
			sky.kind = LightKind::Infinite;
			sky.color = {0.62, 0.72, 0.95};
			sky.intensity = 0.7;
			d.lights.push_back(sky);
			Light sun;
			sun.kind = LightKind::Distant;
			sun.position = {3, 5, 4};
			sun.target = {0, 0, 0};
			sun.color = {1.0, 0.96, 0.9};
			sun.intensity = 3.0;
			d.lights.push_back(sun);
			const std::string text = toPbrt(d);
			EXPECT_NE(text.find("ObjectInstance"), std::string::npos) << toString(shape) << " " << t.name << " should be instanced (a heightmap on a scaled copy is the one case that is not)";
			writeFile(std::string("inst_") + scene_doc::toString(shape) + "__" + t.name + ".pbrt", text);
			writeFile(std::string("flat_") + scene_doc::toString(shape) + "__" + t.name + ".pbrt", toPbrt(d, false));   // (the same scene with every copy written on its own: the baseline of the sweeps)
			++written;
		}
	}
	// The showcase scene (a mix, a subsurface ball, glass, a tiled picture, a projector) under each ready-made lighting, with its own pictures given by absolute path.
	const std::string repoScenes = std::filesystem::absolute("pbrt_scenes").string();
	const std::string presetNames[] = {"studio", "overcast", "sunny", "goldenhour", "moonlit", "lightbox"};
	int p = 0;
	for (scene_doc::lighting::Preset preset : scene_doc::lighting::allPresets()) {
		Document d = scene_doc::makeShowcaseScene();
		for (Object& o : d.objects)
			if (!o.material.imageFile.empty()) o.material.imageFile = repoScenes + "/" + o.material.imageFile;
		scene_doc::lighting::apply(d, preset);
		d.render = {64, 64, 48, 8};
		writeFile("showcase__" + presetNames[p++] + ".pbrt", toPbrt(d));
		++written;
	}
	{   // and as it ships: the studio rig with the window projector
		Document d = scene_doc::makeShowcaseScene();
		for (Object& o : d.objects)
			if (!o.material.imageFile.empty()) o.material.imageFile = repoScenes + "/" + o.material.imageFile;
		for (Light& l : d.lights)
			if (!l.imageFile.empty()) l.imageFile = repoScenes + "/" + l.imageFile;
		d.render = {64, 64, 48, 8};
		writeFile("showcase__asshipped.pbrt", toPbrt(d));
		++written;
	}
	EXPECT_GT(written, 150);
}

// ---------------------------------------------------------------------------------------------------------------------------------
// Instances: copies of one heavy shape are written once (scene_instancing.h)
// ---------------------------------------------------------------------------------------------------------------------------------

namespace {
std::size_t countOf(const std::string& text, const std::string& what) {
	std::size_t n = 0;
	for (std::size_t at = text.find(what); at != std::string::npos; at = text.find(what, at + what.size())) ++n;
	return n;
}

Document forestDocument(int copies) {
	Document d = makeStarterScene();
	d.objects.clear();
	d.lights.clear();
	Object pine = makeObject(ShapeKind::Cone, "Pine");
	pine.radius = 0.5;
	pine.height = 2.0;
	pine.position = {0, 1.0, 0};
	pine.material.color = {0.1, 0.5, 0.1};
	d.objects.push_back(pine);
	scene_doc::ScatterParams p;
	p.count = copies;
	p.width = 10.0;
	p.tiltDegrees = 5.0;
	for (const Object& o : scene_doc::scatter({pine}, p, d.objects).objects) d.objects.push_back(o);
	return d;
}
}  // namespace

TEST(SceneInstancingTest, CopiesOfAHeavyShapeAreWrittenOnceAndPlacedPerCopy) {
	const Document d = forestDocument(20);
	ASSERT_EQ(d.objects.size(), 21u);
	const std::string text = toPbrt(d), flat = toPbrt(d, false);
	EXPECT_EQ(countOf(text, "ObjectBegin"), 1u);
	EXPECT_EQ(countOf(text, "ObjectInstance"), 21u) << "the original is a placement too";
	EXPECT_EQ(countOf(flat, "ObjectBegin"), 0u);
	EXPECT_LT(text.size() * 5, flat.size()) << "the triangles are stated once, not once per copy";
	// The scale of a copy is the factor of its lengths over the template's (a scatter scales each copy by 0.8 to 1.2).
	EXPECT_GT(countOf(text, "  Scale "), 15u);
	const pbrt_load::LoadResult r = loadText(text, "forest_instanced");
	ASSERT_TRUE(r.ok) << r.error;
	EXPECT_EQ(r.scene.instances.size(), 21u);
	ASSERT_EQ(r.scene.groups.size(), 1u);
	EXPECT_EQ(r.scene.groups[0].triangles.size(), scene_doc::coneMesh(0.5, 2.0).triangleCount());
	EXPECT_EQ(r.scene.triangles.size(), 0u);
	// Each copy is still one object of the file, in the document's order (the rendered viewport numbers them like that).
	EXPECT_EQ(r.scene.shapeRanges.size(), 21u);
	Document back;
	std::string err;
	ASSERT_TRUE(fromPbrt(text, back, err)) << err;
	EXPECT_EQ(toJson(back), toJson(d));
}

TEST(SceneInstancingTest, OnlyRealCopiesAreInstanced) {
	{   // a different colour on one: it is no copy; the others still are
		Document d = forestDocument(5);
		d.objects[3].material.color = {0.9, 0.1, 0.1};
		const scene_doc::InstancePlan plan = scene_doc::planInstances(d);
		EXPECT_EQ(plan.templates(), 1);
		EXPECT_EQ(plan.templateOf[3], -1);
		EXPECT_EQ(plan.templateOf[2], 0);
	}
	{   // lengths that are not all in proportion: a taller one is a different shape
		Document d = forestDocument(3);
		d.objects[1].height *= 1.5;
		const scene_doc::InstancePlan plan = scene_doc::planInstances(d);
		EXPECT_EQ(plan.templateOf[1], -1);
	}
	{   // a lone object is written plainly
		Document d = forestDocument(0);
		EXPECT_EQ(scene_doc::planInstances(d).templates(), 0);
		EXPECT_EQ(countOf(toPbrt(d), "ObjectBegin"), 0u);
	}
	{   // light shapes (a sphere is one line of text), emitters, volumes and patterns are never instanced
		Document d = makeStarterScene();
		d.objects.clear();
		for (int i = 0; i < 4; ++i) d.objects.push_back(makeObject(ShapeKind::Sphere, "Ball"));
		for (int i = 0; i < 3; ++i) {
			Object lamp = makeObject(ShapeKind::Cone, "Lamp");
			lamp.emissive = true;
			d.objects.push_back(lamp);
		}
		for (int i = 0; i < 3; ++i) {
			Object marble = makeObject(ShapeKind::Torus, "Marble ring");
			marble.material.pattern = Pattern::Marble;
			d.objects.push_back(marble);
		}
		for (int i = 0; i < 3; ++i) d.objects.push_back(scene_doc::makeVolume(VolumeKind::Fog, "Fog"));
		EXPECT_EQ(scene_doc::planInstances(d).templates(), 0);
		EXPECT_EQ(countOf(toPbrt(d), "ObjectBegin"), 0u);
	}
	{   // two terrains with another seed are two different grounds
		Document d = makeStarterScene();
		d.objects.clear();
		Object a = makeObject(ShapeKind::Terrain, "Hills");
		Object b = a;
		b.seed = 2;
		d.objects = {a, b};
		EXPECT_EQ(scene_doc::planInstances(d).templates(), 0);
		b.seed = 1;
		b.position = {20, 0, 0};
		b.rotation = {0, 90, 0};
		d.objects = {a, b};
		EXPECT_EQ(scene_doc::planInstances(d).templates(), 1) << "the same ground moved and turned is a copy";
	}
	{   // a height map's relief does not scale with an instance, so only an equal-sized copy may share it
		Document d = forestDocument(2);
		for (Object& o : d.objects) o.material.bumpFile = "bump.png";
		d.objects[1].radius = d.objects[0].radius * 2;
		d.objects[1].height = d.objects[0].height * 2;
		d.objects[1].size = {d.objects[0].size.x * 2, d.objects[0].size.y * 2, d.objects[0].size.z * 2};
		d.objects[1].radius2 = d.objects[0].radius2 * 2;
		d.objects[1].meshScale = d.objects[0].meshScale * 2;
		EXPECT_EQ(scene_doc::planInstances(d).templates(), 0);
	}
}

// The same forest as instances and as plain shapes must give the same picture.
TEST(SceneInstancingTest, InstancedForestRendersLikeThePlainOne) {
	const std::string launcher = findLauncher();
	if (launcher.empty()) GTEST_SKIP() << "ray_tracer.exe was not found next to the tests";
	Document d = forestDocument(12);
	Object floor = makeObject(ShapeKind::Quad, "Floor");
	floor.size = {14, 1, 14};
	d.objects.push_back(floor);
	d.camera.position = {0, 9, 11};
	d.camera.target = {0, 0.5, 0};
	d.camera.fov = 45;
	Light sky;
	sky.kind = LightKind::Infinite;
	sky.color = {0.7, 0.8, 1.0};
	sky.intensity = 0.8;
	d.lights.push_back(sky);
	Light sun;
	sun.kind = LightKind::Distant;
	sun.position = {-4, 8, 5};
	sun.target = {0, 0, 0};
	sun.intensity = 3.0;
	d.lights.push_back(sun);
	ASSERT_FALSE(hasErrors(validate(d)));
	std::vector<float> a, b;
	int wa = 0, ha = 0, wb = 0, hb = 0;
	ASSERT_TRUE(renderWithLauncher(launcher, toPbrt(d), "forest_inst", 48, 96, 4, a, wa, ha));
	ASSERT_TRUE(renderWithLauncher(launcher, toPbrt(d, false), "forest_flat", 48, 96, 4, b, wb, hb));
	ASSERT_EQ(a.size(), b.size());
	for (int c = 0; c < 3; ++c) EXPECT_NEAR(channelMean(a, c), channelMean(b, c), 0.02 * channelMean(b, c)) << "channel " << c;
	// ...and where the trees stand: the pictures agree by blocks of 8 x 8 pixels (the noise of 96 samples averages out there)
	double worst = 0.0;
	for (int by = 0; by + 8 <= ha; by += 8)
		for (int bx = 0; bx + 8 <= wa; bx += 8) {
			double sa = 0, sb = 0;
			for (int y = by; y < by + 8; ++y)
				for (int x = bx; x < bx + 8; ++x)
					for (int c = 0; c < 3; ++c) {
						sa += a[3 * (static_cast<std::size_t>(y) * wa + x) + c];
						sb += b[3 * (static_cast<std::size_t>(y) * wa + x) + c];
					}
			worst = std::max(worst, std::fabs(sa - sb) / (3 * 64));
		}
	EXPECT_LT(worst, 0.05) << "the largest difference between two blocks, per channel";
}

// A sphere's pole in pbrt is its own z axis: the Builder stands it up (as a disk) so that a picture or a checker on it runs round a vertical axis, the way a globe does.
TEST(SceneDocumentPbrtTest, ASphereIsStoodUpSoItsTexturePolesAreVertical) {
	Document d = makeStarterScene();
	d.objects.clear();
	d.lights.clear();
	Object ball = makeObject(ShapeKind::Sphere, "Ball");
	ball.position = {1, 2, 3};
	ball.radius = 0.7;
	d.objects.push_back(ball);
	const std::string text = toPbrt(d);
	EXPECT_NE(text.find("Rotate -90 1 0 0\n  Shape \"sphere\""), std::string::npos) << text;
	const pbrt_load::LoadResult r = loadText(text, "sphere_pole");
	ASSERT_TRUE(r.ok) << r.error;
	ASSERT_EQ(r.scene.spheres.size(), 1u);
	const auto& rot = r.scene.spheres[0].uvRot;
	// world up carried into the frame the sphere's own (u, v) are defined in is that frame's pole (y): v = 1 at the top of the ball
	EXPECT_NEAR(rot[3] * 0 + rot[4] * 1 + rot[5] * 0, 1.0, 1e-12);
	EXPECT_NEAR(rot[0] * 0 + rot[1] * 1 + rot[2] * 0, 0.0, 1e-12);
	EXPECT_NEAR(rot[6] * 0 + rot[7] * 1 + rot[8] * 0, 0.0, 1e-12);
}

// ---------------------------------------------------------------------------------------------------------------------------------
// Dots: pbrt's polka-dot texture as a material pattern

TEST(SceneDotsPatternTest, DotsAreWrittenAsPbrtsDotsTextureAndTheRendererLoadsThem) {
	Document d = makeStarterScene();
	Object spotted = makeObject(ShapeKind::Quad, "Spotted");
	spotted.material.pattern = Pattern::Dots;
	spotted.material.color = {0.9, 0.1, 0.1};
	spotted.material.color2 = {0.95, 0.95, 0.9};
	spotted.material.checkerCount = 12.0;
	d.objects.push_back(spotted);
	Object coated = makeObject(ShapeKind::Sphere, "Coated spots");
	coated.material.kind = MaterialKind::CoatedDiffuse;
	coated.material.pattern = Pattern::Dots;
	d.objects.push_back(coated);
	ASSERT_FALSE(hasErrors(validate(d)));
	const std::string text = toPbrt(d);
	EXPECT_NE(text.find("\"spectrum\" \"dots\" \"float uscale\" [ 12 ] \"float vscale\" [ 12 ] \"rgb inside\" [ 0.95 0.95 0.9 ] \"rgb outside\" [ 0.9 0.1 0.1 ]"), std::string::npos) << text;
	const pbrt_load::LoadResult r = loadText(text, "dots_pattern");
	ASSERT_TRUE(r.ok) << r.error;
	int dotted = 0;
	for (const auto& m : r.scene.materials) {
		if (!m.hasDotsReflectance) continue;
		++dotted;
		if (m.dotsInsideColor[0] > 0.85) {   // the quad's (the sphere's use the default colours)
			EXPECT_NEAR(m.dotsInsideColor[1], 0.1, 1e-9);
			EXPECT_NEAR(m.dotsOutsideColor[2], 0.9, 1e-9);
		}
	}
	EXPECT_EQ(dotted, 2) << "both the matte and the glossy paint object carry the dots";
	Document back;
	std::string error;
	ASSERT_TRUE(fromPbrt(text, back, error)) << error;
	EXPECT_EQ(toJson(back), toJson(d)) << "a saved scene reopens with its dots";
}

TEST(SceneDotsPatternTest, ADotsPatternNeedsACountAndANonBlankMaterialKind) {
	Document d = makeStarterScene();
	Object o = makeObject(ShapeKind::Quad, "Spotted");
	o.material.pattern = Pattern::Dots;
	o.material.checkerCount = 0.0;
	d.objects.push_back(o);
	EXPECT_TRUE(hasErrors(validate(d))) << "no cells across";
	d.objects.back().material.checkerCount = 6.0;
	EXPECT_FALSE(hasErrors(validate(d)));
	d.objects.back().material.kind = MaterialKind::Dielectric;
	bool warned = false;
	for (const Problem& p : validate(d)) warned = warned || p.message.find("dots patterns only apply") != std::string::npos;
	EXPECT_TRUE(warned);
}

// ---------------------------------------------------------------------------------------------------------------------------------
// 3D text

namespace {

// Stands in for the GUI's font: one block per character, 1 wide and 2 tall with a gap of 0.5.
bool blockFont(const scene_doc::TextParams& p, scene_doc::TextOutline& out) {
	std::size_t column = 0;
	double row = 0;
	for (char c : p.text) {
		if (c == '\n') { column = 0; row -= 3.0; continue; }
		if (c == ' ') { ++column; continue; }
		const double x = column * 1.5;
		out.contours.push_back({{x, row}, {x + 1.0, row}, {x + 1.0, row + 2.0}, {x, row + 2.0}});
		++column;
	}
	out.capHeight = 2.0;
	return !out.contours.empty();
}

struct WithBlockFont {
	WithBlockFont() { scene_doc::setTextOutlineProvider(&blockFont); }
	~WithBlockFont() { scene_doc::setTextOutlineProvider(nullptr); }
};

}  // namespace

TEST(SceneTextObjectTest, ANewTextObjectHasSensibleDefaultsAndRoundTripsThroughTheDocument) {
	Object o = makeObject(ShapeKind::Text, "Words");
	EXPECT_EQ(o.text.text, "Text");
	EXPECT_DOUBLE_EQ(o.size.x, 1.0) << "no stretch";
	EXPECT_DOUBLE_EQ(o.size.y, 1.0) << "a capital letter a unit tall";
	EXPECT_GT(o.size.z, 0.0);
	EXPECT_TRUE(isGeneratedShape(ShapeKind::Text));
	o.text.text = std::string("Hi\nthere \xC3\xA9 \"quoted\"");
	o.text.font = "Times New Roman";
	o.text.bold = true;
	o.text.italic = true;
	o.text.align = 2;
	o.size = {1.2, 0.8, 0.3};
	Document d = makeStarterScene();
	d.objects.push_back(o);
	Document back;
	std::string error;
	ASSERT_TRUE(fromJson(toJson(d), back, error)) << error;
	ASSERT_EQ(back.objects.size(), d.objects.size());
	const Object& r = back.objects.back();
	EXPECT_EQ(r.shape, ShapeKind::Text);
	EXPECT_EQ(r.text.text, o.text.text);
	EXPECT_EQ(r.text.font, "Times New Roman");
	EXPECT_TRUE(r.text.bold);
	EXPECT_TRUE(r.text.italic);
	EXPECT_EQ(r.text.align, 2);
	EXPECT_DOUBLE_EQ(r.size.y, 0.8);
	EXPECT_EQ(toJson(back), toJson(d));
}

TEST(SceneTextObjectTest, OnlyATextObjectWritesTextFieldsToTheDocument) {
	const Document d = makeStarterScene();
	EXPECT_EQ(toJson(d).find("\"italic\""), std::string::npos) << "scenes without text write exactly what they always did";
}

TEST(SceneTextObjectTest, TheTextIsWrittenAsATriangleMeshTheRendererLoads) {
	WithBlockFont font;
	Document d = makeStarterScene();
	Object o = makeObject(ShapeKind::Text, "Words");
	o.text.text = "AB";
	o.material.color = {0.2, 0.4, 0.9};
	d.objects.push_back(o);
	ASSERT_FALSE(hasErrors(validate(d)));
	const std::string text = toPbrt(d);
	EXPECT_NE(text.find("Shape \"trianglemesh\""), std::string::npos);
	const pbrt_load::LoadResult r = loadText(text, "text_object");
	ASSERT_TRUE(r.ok) << r.error;
	EXPECT_GE(r.scene.triangles.size(), 2u * 2u * 3u) << "front, back and four walls for each of the two letters";
	Document back;
	std::string error;
	ASSERT_TRUE(fromPbrt(text, back, error)) << error;
	EXPECT_EQ(toJson(back), toJson(d)) << "a saved scene reopens with its text";
}

TEST(SceneTextObjectTest, ChangingTheTextOrTheSizeChangesTheMesh) {
	WithBlockFont font;
	Object o = makeObject(ShapeKind::Text, "Words");
	o.text.text = "AB";
	const std::size_t two = generatedMesh(o).triangleCount();
	o.text.text = "ABC";
	EXPECT_EQ(generatedMesh(o).triangleCount(), two / 2 * 3);
	const auto width = [](const Object& t) {
		const scene_doc::ShapeMesh m = generatedMesh(t);
		double lo = 1e9, hi = -1e9;
		for (std::size_t i = 0; i < m.P.size(); i += 3) { lo = std::min(lo, m.P[i]); hi = std::max(hi, m.P[i]); }
		return hi - lo;
	};
	const double w = width(o);
	o.size.y *= 2;
	EXPECT_NEAR(width(o), 2 * w, 1e-9) << "twice the height, twice the width";
	o.size.x = 1.5;
	EXPECT_NEAR(width(o), 3 * w, 1e-9) << "and stretched by a half again";
}

TEST(SceneTextObjectTest, ATextWithNoLettersWritesNoShapeAndIsReported) {
	WithBlockFont font;
	Document d = makeStarterScene();
	Object o = makeObject(ShapeKind::Text, "Nothing");
	o.text.text = "  ";
	d.objects.push_back(o);
	EXPECT_FALSE(hasErrors(validate(d)));
	bool warned = false;
	for (const Problem& p : validate(d)) warned = warned || p.message.find("text is empty") != std::string::npos;
	EXPECT_TRUE(warned);
	const pbrt_load::LoadResult r = loadText(toPbrt(d), "text_empty");
	EXPECT_TRUE(r.ok) << r.error;
	d.objects.back().text.text = "x";
	d.objects.back().size.y = 0.0;
	EXPECT_TRUE(hasErrors(validate(d))) << "a text with no height";
}

TEST(SceneTextObjectTest, DuplicatingAndScalingATextKeepsItsLettersInProportion) {
	Object o = makeObject(ShapeKind::Text, "Words");
	o.text.text = "Hello";
	o.size = {1.4, 1.0, 0.25};
	scaleObject(o, 2.0);
	EXPECT_DOUBLE_EQ(o.size.x, 1.4) << "the stretch is not a length";
	EXPECT_DOUBLE_EQ(o.size.y, 2.0);
	EXPECT_DOUBLE_EQ(o.size.z, 0.5);
	EXPECT_GT(boundingRadius(o), 1.0);
	EXPECT_GT(footprintRadius(o), 0.0);
}
