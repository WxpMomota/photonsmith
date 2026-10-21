/**
 * @file pbrt_cpu_builder_features_tests.cpp
 * @brief pbrt_cpu builder tests: cones and paraboloids as lights and media, sphere uv, emitters that reflect, the dots mapping, curve hits
 * Helpers are in pbrt_cpu_builder_test_helpers.h; one part of what used to be a single pbrt_cpu_builder_tests.cpp.
 */

#include "pbrt_cpu_builder_test_helpers.h"

// ===========================================================================
// Cone/Paraboloid real AreaLightSource/MediumInterface support - see
// pbrt_flatten::Cone/Paraboloid's own comment for the feature this
// round closed.
// ===========================================================================

TEST(PbrtCpuBuildTest, ConeWithAreaLightIsAddedToLightsAndIsSampleable) {
	const pbrt_cpu::BuildResult b = buildFrom(
		"AttributeBegin\n"
		"  AreaLightSource \"diffuse\" \"rgb L\" [ 4 4 4 ]\n"
		"  Shape \"cone\" \"float radius\" [ 1 ] \"float height\" [ 2 ]\n"
		"AttributeEnd\n");
	ASSERT_EQ(b.lights->objects.size(), 1u);
	auto cone = std::dynamic_pointer_cast<cone_hittable>(b.lights->objects[0]);
	ASSERT_NE(cone, nullptr);

	// Real solid-angle NEE sampling (ConeShape::sample_from/pdf_from, see
	// their own comments in shapes.h) - a point well outside the cone must
	// get a real, positive pdf both ways (sample and re-query).
	const point3 origin(-5, 0, 1);
	bool sawPositivePdf = false;
	for (int i = 0; i < 20; ++i) {
		const vec3 dir = cone->random(origin);
		const double pdf = cone->pdf_value(origin, dir);
		if (pdf > 0.0) sawPositivePdf = true;
	}
	EXPECT_TRUE(sawPositivePdf);
}

TEST(PbrtCpuBuildTest, ConeWithMediumScattersReliably) {
	// Material "none" (a real Interface material - MaterialKind::Interface's
	// own comment, pbrt_flatten.h) keeps the cone's own boundary invisible,
	// so the medium's own scatter event (not the opaque boundary surface)
	// is what a ray actually observes - see kd_tree_hittable_tests.cpp's own
	// top comment for why this matters (an opaque boundary always wins the
	// closest-hit race over a scatter event drawn from inside the volume).
	const pbrt_cpu::BuildResult b = buildFrom(
		"MakeNamedMedium \"fog\" \"string type\" \"homogeneous\"\n"
		"  \"rgb sigma_a\" [ 0 0 0 ] \"rgb sigma_s\" [ 500 500 500 ]\n"
		"AttributeBegin\n"
		"  MediumInterface \"fog\" \"\"\n"
		"  Material \"none\"\n"
		"  Shape \"cone\" \"float radius\" [ 1 ] \"float height\" [ 2 ]\n"
		"AttributeEnd\n");
	ASSERT_EQ(b.coneCount, 1u);
	// A ray along the base circle's own diameter (z=0, matching
	// ShapesCone.IntersectAtBase's identical setup in shapes_tests.cpp) -
	// crosses the hollow lateral surface TWICE (x=-1 entering, x=+1
	// exiting), the real [t0,t1] entry/exit pair constant_medium's own
	// hit() needs. A ray parallel to the axis instead (e.g. straight along
	// +z) only ever crosses this open, uncapped shell ONCE - not a valid
	// medium interval at all, not a real test of scattering.
	for (int i = 0; i < 200; ++i) {
		hit_record rec;
		ASSERT_TRUE(b.world->hit(ray(point3(-5, 0, 0), vec3(1, 0, 0)),
								 interval(0.001, infinity), rec))
			<< "iteration " << i << ": a sigma_s=500 medium across the cone's "
			   "own radius-1 base (optical depth ~1000) must scatter on "
			   "essentially every ray";
	}
}

TEST(PbrtCpuBuildTest, ConeWithMediumScattersReliablyEnteringThroughTheOpenBase) {
	// Regression test for the medium-boundary bug a code-review pass found:
	// constant_medium's generic "two sequential hit() calls" technique
	// needs TWO lateral-surface crossings, but a ray entering through the
	// cone's own open base (no cap there - intersect() only ever models
	// the lateral surface) and exiting through the lateral wall produces
	// only ONE such crossing, so the old code silently reported "no medium
	// here" for exactly this ray shape - the case
	// ConeWithMediumScattersReliably's own ray (crossing the lateral wall
	// twice) can't exercise. Now handled by cone_hittable::volume_bounds()
	// (a real "infinite quadric intersected with a z-slab" solve,
	// mirroring cylinder_hittable's own identical fix for its own open
	// ends - see hittable::volume_bounds()'s own comment).
	const pbrt_cpu::BuildResult b = buildFrom(
		"MakeNamedMedium \"fog\" \"string type\" \"homogeneous\"\n"
		"  \"rgb sigma_a\" [ 0 0 0 ] \"rgb sigma_s\" [ 500 500 500 ]\n"
		"AttributeBegin\n"
		"  MediumInterface \"fog\" \"\"\n"
		"  Material \"none\"\n"
		"  Shape \"cone\" \"float radius\" [ 1 ] \"float height\" [ 2 ]\n"
		"AttributeEnd\n");
	ASSERT_EQ(b.coneCount, 1u);
	// Enters the volume through the open base (z=0, x=0.3 - well inside
	// the radius-1 base disk, no surface hit there) travelling straight up
	// the +z axis, and exits through the lateral wall at z=1.4 (where
	// radius-k*z == 0.3, k=radius/height=0.5) - exactly ONE lateral-
	// surface crossing.
	for (int i = 0; i < 200; ++i) {
		hit_record rec;
		ASSERT_TRUE(b.world->hit(ray(point3(0.3, 0, -5), vec3(0, 0, 1)),
								 interval(0.001, infinity), rec))
			<< "iteration " << i << ": a sigma_s=500 medium across ~1.4 units "
			   "of the cone's interior (optical depth ~700) must scatter on "
			   "essentially every ray, even entering through the open base";
	}
}

TEST(PbrtCpuBuildTest, ParaboloidWithAreaLightIsAddedToLightsAndIsSampleable) {
	const pbrt_cpu::BuildResult b = buildFrom(
		"AttributeBegin\n"
		"  AreaLightSource \"diffuse\" \"rgb L\" [ 4 4 4 ]\n"
		"  Shape \"paraboloid\" \"float radius\" [ 1 ]\n"
		"AttributeEnd\n");
	ASSERT_EQ(b.lights->objects.size(), 1u);
	auto para = std::dynamic_pointer_cast<paraboloid_hittable>(b.lights->objects[0]);
	ASSERT_NE(para, nullptr);

	const point3 origin(-5, 0, 0.5);
	bool sawPositivePdf = false;
	for (int i = 0; i < 20; ++i) {
		const vec3 dir = para->random(origin);
		const double pdf = para->pdf_value(origin, dir);
		if (pdf > 0.0) sawPositivePdf = true;
	}
	EXPECT_TRUE(sawPositivePdf);
}

TEST(PbrtCpuBuildTest, ParaboloidWithMediumScattersReliably) {
	// See ConeWithMediumScattersReliably's own comment - identical reasoning.
	const pbrt_cpu::BuildResult b = buildFrom(
		"MakeNamedMedium \"fog\" \"string type\" \"homogeneous\"\n"
		"  \"rgb sigma_a\" [ 0 0 0 ] \"rgb sigma_s\" [ 500 500 500 ]\n"
		"AttributeBegin\n"
		"  MediumInterface \"fog\" \"\"\n"
		"  Material \"none\"\n"
		"  Shape \"paraboloid\" \"float radius\" [ 1 ]\n"
		"AttributeEnd\n");
	ASSERT_EQ(b.paraboloidCount, 1u);
	// A ray along the rim's own diameter (z=zmax=1, matching
	// ShapesParaboloid.IntersectAtRimMatchesRadius's identical setup) -
	// crosses the hollow surface TWICE (x=-1 entering, x=+1 exiting), the
	// real [t0,t1] entry/exit pair constant_medium's own hit() needs - see
	// ConeWithMediumScattersReliably's own comment for why a ray parallel
	// to the axis instead would never produce a valid interval at all.
	for (int i = 0; i < 200; ++i) {
		hit_record rec;
		ASSERT_TRUE(b.world->hit(ray(point3(-5, 0, 1), vec3(1, 0, 0)),
								 interval(0.001, infinity), rec))
			<< "iteration " << i << ": a sigma_s=500 medium across the "
			   "paraboloid's own radius-1 rim (optical depth ~1000) must "
			   "scatter on essentially every ray";
	}
}

TEST(PbrtCpuBuildTest, ParaboloidWithMediumScattersReliablyEnteringThroughTheOpenRim) {
	// See ConeWithMediumScattersReliablyEnteringThroughTheOpenBase's own
	// comment - identical reasoning, mirrored for a paraboloid's own open
	// end (its rim, at z=zmax; the apex at z=zmin=0 is a single point, not
	// an opening, matching a cone's apex).
	const pbrt_cpu::BuildResult b = buildFrom(
		"MakeNamedMedium \"fog\" \"string type\" \"homogeneous\"\n"
		"  \"rgb sigma_a\" [ 0 0 0 ] \"rgb sigma_s\" [ 500 500 500 ]\n"
		"AttributeBegin\n"
		"  MediumInterface \"fog\" \"\"\n"
		"  Material \"none\"\n"
		"  Shape \"paraboloid\" \"float radius\" [ 1 ]\n"
		"AttributeEnd\n");
	ASSERT_EQ(b.paraboloidCount, 1u);
	// Enters the volume through the open rim (z=zmax=1, x=0.5, y=0.4 - well
	// inside the radius-1 rim, no surface hit there) and exits through the
	// lateral wall around z=0.455 (where x^2+y^2 == z/k, k=zmax/radius^2=1)
	// - exactly ONE lateral-surface crossing. A small (0, 0.1, -1)
	// direction rather than a pure (0,0,-1) axis-parallel one deliberately
	// avoids ParaboloidShape::intersect()'s own separate, pre-existing
	// "a==0 when dx=dy=0" degenerate-miss branch (its own comment, shapes.h)
	// - unrelated to the open-rim case this test targets, and would
	// otherwise mask it by returning a miss for a completely different
	// reason.
	for (int i = 0; i < 200; ++i) {
		hit_record rec;
		ASSERT_TRUE(b.world->hit(ray(point3(0.5, 0, 5), vec3(0, 0.1, -1)),
								 interval(0.001, infinity), rec))
			<< "iteration " << i << ": a sigma_s=500 medium across ~0.55 "
			   "units of the paraboloid's interior (optical depth ~270) "
			   "must scatter on essentially every ray, even entering "
			   "through the open rim";
	}
}

// A sphere loaded from a pbrt file has pbrt's texture coordinates: u = atan2(y, x) / 2 pi and v = 1 - acos(z) / pi about the OBJECT's z axis (pbrt-v4 Sphere::InteractionFromIntersection),
// whatever the sphere's transform is. The loader used to bake a full sphere to a centre and a radius and keep the poles on world y, so a picture on a turned sphere did not turn with it.
namespace {
struct Rot3 { double m[9]; };
// Rotation by `deg` about the axis (ax, ay, az): the matrix of pbrt's Rotate directive.
Rot3 rotation(double deg, double ax, double ay, double az) {
	const double len = std::sqrt(ax * ax + ay * ay + az * az);
	ax /= len; ay /= len; az /= len;
	const double a = deg * 3.14159265358979323846 / 180.0, c = std::cos(a), s = std::sin(a), t = 1 - c;
	return {{t * ax * ax + c, t * ax * ay - s * az, t * ax * az + s * ay,
	         t * ax * ay + s * az, t * ay * ay + c, t * ay * az - s * ax,
	         t * ax * az - s * ay, t * ay * az + s * ax, t * az * az + c}};
}
}  // namespace

TEST(PbrtSphereUv, FollowsPbrtsObjectSpaceParametrisationForATurnedSphere) {
	const Rot3 R = rotation(35.0, 1.0, 2.0, 3.0);
	const pbrt_cpu::BuildResult b = buildFrom(
		"AttributeBegin\n  Translate 1 2 3\n  Rotate 35 1 2 3\n  Shape \"sphere\" \"float radius\" [ 2 ]\nAttributeEnd\n");
	const double pi = 3.14159265358979323846;
	const point3 centre(1, 2, 3);
	int checked = 0;
	for (int i = 0; i < 40; ++i) {
		// a direction from the centre, in the world; the ray comes in along it
		const double az = 0.31 + 0.157 * i, el = -1.3 + 0.067 * i * 1.9;
		vec3 dir(std::cos(el) * std::cos(az), std::sin(el), std::cos(el) * std::sin(az));
		dir = unit_vector(dir);
		hit_record rec;
		const ray r(centre + 10.0 * dir, -dir);
		ASSERT_TRUE(b.world->hit(r, interval(0.001, infinity), rec));
		// the hit point's direction in the object frame: R^T * dir
		const double ox = R.m[0] * dir.x() + R.m[3] * dir.y() + R.m[6] * dir.z();
		const double oy = R.m[1] * dir.x() + R.m[4] * dir.y() + R.m[7] * dir.z();
		const double oz = R.m[2] * dir.x() + R.m[5] * dir.y() + R.m[8] * dir.z();
		double u = std::atan2(oy, ox) / (2 * pi);
		if (u < 0) u += 1.0;
		const double v = 1.0 - std::acos(std::fmin(1.0, std::fmax(-1.0, oz))) / pi;
		const double du = std::fabs(rec.u - u);
		EXPECT_TRUE(du < 1e-9 || std::fabs(du - 1.0) < 1e-9) << "u at i=" << i << ": " << rec.u << " against " << u;
		EXPECT_NEAR(rec.v, v, 1e-9) << "v at i=" << i;
		++checked;
	}
	EXPECT_EQ(checked, 40);
}

// The tangents follow the (u, v): dpdu runs round the object's z axis (increasing phi), dpdv towards the south pole (pbrt's dpdv = -pi * ...), and both are in the world frame.
TEST(PbrtSphereUv, TangentsAreWorldSpaceAndMatchPbrt) {
	const pbrt_cpu::BuildResult b = buildFrom("Shape \"sphere\" \"float radius\" [ 2 ]\n");   // a sphere that is not turned: the pole is world z
	hit_record rec;
	ASSERT_TRUE(b.world->hit(ray(point3(5, 0, 0), vec3(-1, 0, 0)), interval(0.001, infinity), rec));   // the point (2, 0, 0): phi = 0, theta = pi / 2
	EXPECT_NEAR(rec.u, 0.0, 1e-9);
	EXPECT_NEAR(rec.v, 0.5, 1e-9);
	const double pi = 3.14159265358979323846;
	// pbrt: dpdu = phiMax * (-y, x, 0) = 2 pi * (0, 2, 0); dpdv = (thetaZMax - thetaZMin) * (z cos(phi), z sin(phi), -r sin(theta)) = -pi * (0, 0, -2) = (0, 0, 2 pi)
	EXPECT_NEAR(rec.dpdu.x(), 0.0, 1e-9);
	EXPECT_NEAR(rec.dpdu.y(), 2 * pi * 2.0, 1e-9);
	EXPECT_NEAR(rec.dpdu.z(), 0.0, 1e-9);
	EXPECT_NEAR(rec.dpdv.x(), 0.0, 1e-9);
	EXPECT_NEAR(rec.dpdv.y(), 0.0, 1e-9);
	EXPECT_NEAR(rec.dpdv.z(), 2 * pi, 1e-9);
	// the north pole (z = r) is v = 1
	ASSERT_TRUE(b.world->hit(ray(point3(0, 0, 5), vec3(0, 0, -1)), interval(0.001, infinity), rec));
	EXPECT_NEAR(rec.v, 1.0, 1e-6);
}

// ---- An emitter keeps the shape's material in pbrt-v4 -----------------------------------------------------------------------------------------------------------
//
// An AreaLightSource attaches to the shape and the shape keeps the Material in effect (the default is diffuse 0.5), so a lamp lit by another light reflects it. The
// Scene Builder writes a black body (diffuse reflectance 0) for an emitter, which must stay a plain emitter.

namespace {

// Fires a ray at the emissive quad and reports whether its surface scatters, and its emission.
void lampHit(const char *materialLine, bool &scatters, double &emitted) {
	const std::string text = std::string(materialLine) +
		"AreaLightSource \"diffuse\" \"rgb L\" [ 2 2 2 ]\n"
		"Shape \"trianglemesh\" \"integer indices\" [ 0 1 2  0 2 3 ]\n"
		"  \"point3 P\" [ 0 0 0  1 0 0  1 1 0  0 1 0 ]\n";
	const pbrt_cpu::BuildResult b = buildFrom(text);
	hit_record rec;
	const ray r(point3(0.5, 0.5, 3), vec3(0, 0, -1));
	ASSERT_TRUE(b.world->hit(r, interval(0.001, infinity), rec));
	emitted = rec.mat->emitted(r, rec, rec.u, rec.v, rec.p).x();
	scatter_record srec;
	scatters = rec.mat->scatter(r, rec, srec);
}

}  // namespace

TEST(PbrtEmitterReflects, AnEmitterWithTheDefaultMaterialAlsoScattersLight) {
	bool scatters = false;
	double emitted = 0.0;
	lampHit("", scatters, emitted);
	EXPECT_DOUBLE_EQ(emitted, 2.0);
	EXPECT_TRUE(scatters) << "pbrt-v4's lamp keeps the default diffuse material (0.5), so it reflects the light that reaches it";
}

TEST(PbrtEmitterReflects, AnEmitterWithABlackBodyStaysAPlainEmitter) {
	bool scatters = true;
	double emitted = 0.0;
	lampHit("Material \"diffuse\" \"rgb reflectance\" [ 0 0 0 ]\n", scatters, emitted);
	EXPECT_DOUBLE_EQ(emitted, 2.0);
	EXPECT_FALSE(scatters) << "the Scene Builder writes a black body for an emitter: nothing to reflect";
}

TEST(PbrtEmitterReflects, AnEmitterKeepsWhateverMaterialItWasGiven) {
	bool scatters = false;
	double emitted = 0.0;
	lampHit("Material \"conductor\" \"float roughness\" [ 0.3 ]\n", scatters, emitted);
	EXPECT_DOUBLE_EQ(emitted, 2.0);
	EXPECT_TRUE(scatters);
}

// ---- A "dots" Texture's own uscale/vscale/udelta/vdelta -----------------------------------------------------------------------------------------------------
//
// pbrt-v4's DotsTexture counts its cells in the UVMapping's coordinates (su * u + du, sv * v + dv); the renderers used to read every dots texture in the surface's own
// (u, v), so `"float uscale" [ 8 ]` drew one big dot instead of eight across.

namespace {

// The colour the surface scatters at (x, y) of a unit quad whose texture coordinates run to (uvMax, uvMax). (The dots are red: in pbrt-v4 the "outside" parameter colours them.)
color dotsColourAt(const char *mappingParams, double uvMax, double x, double y) {
	const std::string text =
		std::string("Texture \"spots\" \"spectrum\" \"dots\" \"rgb inside\" [ 0 0 1 ] \"rgb outside\" [ 1 0 0 ] ") + mappingParams + "\n"
		"Material \"diffuse\" \"texture reflectance\" [ \"spots\" ]\n"
		"Shape \"trianglemesh\" \"integer indices\" [ 0 1 2  0 2 3 ] \"point3 P\" [ 0 0 0  1 0 0  1 1 0  0 1 0 ]\n"
		"  \"point2 uv\" [ 0 0  " + std::to_string(uvMax) + " 0  " + std::to_string(uvMax) + " " + std::to_string(uvMax) + "  0 " + std::to_string(uvMax) + " ]\n";
	const pbrt_cpu::BuildResult b = buildFrom(text);
	hit_record rec;
	const ray r(point3(x, y, 2), vec3(0, 0, -1));
	if (!b.world->hit(r, interval(0.001, infinity), rec) || !rec.mat) {   // (not an EXPECT then a null dereference: a miss must fail the test, not crash it)
		ADD_FAILURE() << "the ray at (" << x << ", " << y << ") missed the quad";
		return color(0, 0, 0);
	}
	scatter_record srec;
	EXPECT_TRUE(rec.mat->scatter(r, rec, srec));
	return srec.attenuation;
}

}  // namespace

TEST(PbrtDotsMapping, AUscaleCountsTheCellsInTheTexturesOwnCoordinates) {
	int inside = 0, outside = 0;
	for (int i = 0; i < 24; ++i)
		for (int j = 0; j < 24; ++j) {
			const double x = (i + 0.5) / 24.0, y = (j + 0.5) / 24.0 + 0.0123;   // (not exactly x == y: that ray runs along the quad's diagonal, the edge its two triangles share, where it may hit neither on an ARM machine)
			const color scaled = dotsColourAt("\"float uscale\" [ 8 ] \"float vscale\" [ 8 ]", 1.0, x, y);   // the texture's own scale on a quad with uv 0..1
			const color baked = dotsColourAt("", 8.0, x, y);                                                   // the same dots with the scale in the quad's uv
			EXPECT_EQ(scaled.x() > 0.5, baked.x() > 0.5) << "at (" << x << ", " << y << ")";
			(scaled.x() > 0.5 ? inside : outside)++;
		}
	EXPECT_GT(inside, 20) << "dots show up";
	EXPECT_GT(outside, inside) << "on a ground";
}

TEST(PbrtDotsMapping, TheDeltasMoveTheCells) {
	for (int i = 0; i < 9; ++i) {   // (x + 0.125 stays on the quad)
		const double x = (i + 0.5) / 12.0, y = 0.37;
		const color moved = dotsColourAt("\"float uscale\" [ 4 ] \"float vscale\" [ 4 ] \"float udelta\" [ 0.5 ] \"float vdelta\" [ 0.25 ]", 1.0, x, y);
		const color reference = dotsColourAt("\"float uscale\" [ 4 ] \"float vscale\" [ 4 ]", 1.0, x + 0.5 / 4.0, y + 0.25 / 4.0);   // the same point seen through the shift
		EXPECT_EQ(moved.x() > 0.5, reference.x() > 0.5) << "at x = " << x;
	}
}

// ---- Rays leave a curve from outside its strand ------------------------------------------------------------------------------------------------------------
//
// A curve's hit is reported where the ray passes closest to the strand's axis, which is inside the tube. pbrt-v4 spawns every ray from a point pushed out by the curve's
// width (its error bound); rays that left from inside the strand met it again and a strand read 17% too dark (curve-tuft).

TEST(PbrtCurveHit, TheRayOriginIsOutsideTheStrand) {
	const pbrt_cpu::BuildResult b = buildFrom(
		"Shape \"curve\" \"string type\" [ \"cylinder\" ] \"float width0\" [ 0.2 ] \"float width1\" [ 0.2 ]\n"
		"  \"point3 P\" [ 0 0 0  0 1 0  0 2 0  0 3 0 ]\n");
	hit_record rec;
	const ray r(point3(0.03, 1.5, 3), vec3(0, 0, -1));
	ASSERT_TRUE(b.world->hit(r, interval(0.001, infinity), rec));
	EXPECT_NEAR(rec.t, 3.0, 1e-6) << "the reported distance is still pbrt's: the depth of the axis";
	const double fromAxis = std::hypot(rec.p.x(), rec.p.z());
	EXPECT_GT(fromAxis, 0.1) << "a ray spawned here starts outside the strand (radius 0.1)";
	EXPECT_GT(dot(rec.normal, vec3(0, 0, 1)), 0.0) << "the normal faces the side the ray arrived on";
	// A ray leaving that point towards the side it arrived on does not hit the strand it came from.
	hit_record again;
	const ray leaving(rec.p, unit_vector(vec3(0.5, 0.2, 1.0)));
	EXPECT_FALSE(b.world->hit(leaving, interval(0.001, infinity), again));
}

// ---- A reflectance that is a graph of textures ----------------------------------------------------------------------------------------------------------------
//
// pbrt_flatten::Material::reflectanceGraph: each recipe node becomes a texture over the nodes before it.

TEST(PbrtReflectanceGraph, ProductsAndBlendsOfConstantsEvaluateLikePbrt) {
	using Node = pbrt_flatten::TexMeanNode;
	std::vector<Node> nodes(5);
	nodes[0].c[0] = 0.8; nodes[0].c[1] = 0.4; nodes[0].c[2] = 0.2;   // a colour
	nodes[1].c[0] = nodes[1].c[1] = nodes[1].c[2] = 0.5;               // a float
	nodes[2].kind = Node::Kind::Product; nodes[2].a = 0; nodes[2].b = 1;
	nodes[3].c[0] = nodes[3].c[1] = nodes[3].c[2] = 0.25;              // the blend's amount
	nodes[4].kind = Node::Kind::Blend; nodes[4].a = 0; nodes[4].b = 2; nodes[4].t = 3;
	const color c = pbrt_cpu::detail::reflectanceGraphTexture(nodes)->value(0.3, 0.7, point3(1, 2, 3));
	EXPECT_NEAR(c.x(), 0.75 * 0.8 + 0.25 * 0.4, 1e-12) << "(1 - t) * a + t * (a * b)";
	EXPECT_NEAR(c.y(), 0.75 * 0.4 + 0.25 * 0.2, 1e-12);
	EXPECT_NEAR(c.z(), 0.75 * 0.2 + 0.25 * 0.1, 1e-12);
}
