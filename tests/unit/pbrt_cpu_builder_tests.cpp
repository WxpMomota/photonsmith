/**
 * @file pbrt_cpu_builder_tests.cpp
 * @brief pbrt_cpu builder tests: geometry, materials, lights and cameras built from a flattened scene
 * Helpers are in pbrt_cpu_builder_test_helpers.h; one part of what used to be a single pbrt_cpu_builder_tests.cpp.
 */

#include "pbrt_cpu_builder_test_helpers.h"

TEST(PbrtCpuBuildTest, GeometryIsWhereTheSceneSaysItIs) {
	const pbrt_cpu::BuildResult b = buildFrom(kQuad);
	EXPECT_EQ(b.triangleCount, 2u);

	double t = 0.0;
	ASSERT_TRUE(castRay(b, point3(0.5, 0.5, -5), vec3(0, 0, 1), t));
	EXPECT_NEAR(t, 5.0, 1e-6);

	EXPECT_FALSE(castRay(b, point3(9, 9, -5), vec3(0, 0, 1), t))
		<< "a ray well outside the quad must miss";
}

TEST(PbrtCpuBuildTest, TranslationInTheSceneMovesTheGeometry) {
	const pbrt_cpu::BuildResult b = buildFrom(std::string("Translate 0 0 10\n") + kQuad);
	double t = 0.0;
	ASSERT_TRUE(castRay(b, point3(0.5, 0.5, -5), vec3(0, 0, 1), t));
	EXPECT_NEAR(t, 15.0, 1e-6) << "the quad moved 10 further away";
}

TEST(PbrtCpuBuildTest, SphereIsBuiltAtItsTransformedCentreAndRadius) {
	const pbrt_cpu::BuildResult b = buildFrom(
		"Translate 0 0 10\n"
		"Scale 2 2 2\n"
		"Shape \"sphere\" \"float radius\" [ 1 ]\n");
	EXPECT_EQ(b.sphereCount, 1u);
	double t = 0.0;
	// Centre at z=10, radius 2, so the near surface sits at z=8.
	ASSERT_TRUE(castRay(b, point3(0, 0, 0), vec3(0, 0, 1), t));
	EXPECT_NEAR(t, 8.0, 1e-6);
}

TEST(PbrtCpuBuildTest, SphereZMaxClipsAwayTheTopCap) {
	// A horizontal ray through the removed cap band (z=0.9, above zmax=0.5)
	// crosses where the full sphere's surface would be (both roots land at
	// z=0.9 since the ray never changes z) but the clipped sphere has no
	// surface left there at all.
	const pbrt_cpu::BuildResult full = buildFrom(
		"Shape \"sphere\" \"float radius\" [ 1 ]\n");
	double t = 0.0;
	ASSERT_TRUE(castRay(full, point3(-5, 0, 0.9), vec3(1, 0, 0), t))
		<< "sanity check: the full sphere's belt at z=0.9 is really there";

	const pbrt_cpu::BuildResult clipped = buildFrom(
		"Shape \"sphere\" \"float radius\" [ 1 ] \"float zmax\" [ 0.5 ]\n");
	EXPECT_FALSE(castRay(clipped, point3(-5, 0, 0.9), vec3(1, 0, 0), t))
		<< "zmax=0.5 removes the whole z=0.9 band, so this ray should miss entirely";
}

TEST(PbrtCpuBuildTest, SphereZMinFallsThroughToTheFarRootWhenTheNearOneIsClipped) {
	// zmin=0 keeps only the upper hemisphere. A ray straight up through the
	// centre hits the (clipped-away) bottom pole first on a full sphere, but
	// must fall through to the (kept) top pole on the clipped one.
	const pbrt_cpu::BuildResult full = buildFrom(
		"Shape \"sphere\" \"float radius\" [ 1 ]\n");
	double tFull = 0.0;
	ASSERT_TRUE(castRay(full, point3(0, 0, -5), vec3(0, 0, 1), tFull));
	EXPECT_NEAR(tFull, 4.0, 1e-6) << "hits the near (bottom) pole at z=-1";

	const pbrt_cpu::BuildResult clipped = buildFrom(
		"Shape \"sphere\" \"float radius\" [ 1 ] \"float zmin\" [ 0 ]\n");
	double tClipped = 0.0;
	ASSERT_TRUE(castRay(clipped, point3(0, 0, -5), vec3(0, 0, 1), tClipped));
	EXPECT_NEAR(tClipped, 6.0, 1e-6)
		<< "bottom pole is clipped away, so this must fall through to the top pole at z=1";
}

TEST(PbrtCpuBuildTest, SpherePhiMaxClipsAnAzimuthalWedgeFallingThroughToTheFarRoot) {
	// phimax=90 keeps only phi in [0,90] degrees (pbrt-v4's atan2(y,x)
	// convention). A ray through the centre along X hits phi=180 (rejected)
	// on the near side and phi=0 (kept, boundary-inclusive) on the far side.
	const pbrt_cpu::BuildResult clipped = buildFrom(
		"Shape \"sphere\" \"float radius\" [ 1 ] \"float phimax\" [ 90 ]\n");
	double t = 0.0;
	ASSERT_TRUE(castRay(clipped, point3(-5, 0, 0), vec3(1, 0, 0), t));
	EXPECT_NEAR(t, 6.0, 1e-6)
		<< "phi=180 near root clipped away, falls through to phi=0 at x=1";
}

TEST(PbrtCpuBuildTest, ClippedSphereUnderRotationHitsAtTheExactTransformedPosition) {
	// Mirrors DiskUnderRotationHitsAtTheExactTransformedPosition below: a
	// clipped sphere is no longer rotation-invariant (see pbrt_flatten::
	// Sphere's own comment), so this proves the real object-to-world
	// transform - not just the plain baked centre/radius approximation - is
	// what the clipped path actually uses.
	//
	// zmin=0 keeps local z>=0 (the "north" hemisphere). Rotate 90 degrees
	// about X maps local (0,0,1) (kept) to world (0,-1,0), and local
	// (0,0,-1) (clipped away) to world (0,1,0).
	const pbrt_cpu::BuildResult b = buildFrom(
		"Rotate 90 1 0 0\n"
		"Shape \"sphere\" \"float radius\" [ 1 ] \"float zmin\" [ 0 ]\n");
	double t = 0.0;
	// A ray from world (0,5,0) toward -Y hits world (0,1,0) first - the
	// image of the CLIPPED-away south pole - so it must fall through to the
	// far root at world (0,-1,0), the image of the kept north pole.
	ASSERT_TRUE(castRay(b, point3(0, 5, 0), vec3(0, -1, 0), t));
	EXPECT_NEAR(t, 6.0, 1e-6)
		<< "if rotation weren't applied to the clip test, this would either "
		   "hit at t=4 (clip ignored) or miss entirely (wrong hemisphere clipped)";
}

TEST(PbrtCpuBuildTest, ClippedSphereDpdvDoesNotVanishAtThePole) {
	// phimax=90 clips azimuthally only, leaving both poles reachable - a ray
	// straight down onto the north pole exercises sphere_clipped_hittable's
	// degenerate-pole fallback. Only dpdu has a genuine coordinate
	// singularity there (like longitude lines converging at a globe's
	// pole); dpdv (a line of latitude/theta) has a well-defined, nonzero
	// length-rate right at the pole and must not collapse toward zero.
	const pbrt_cpu::BuildResult b = buildFrom(
		"Shape \"sphere\" \"float radius\" [ 1 ] \"float phimax\" [ 90 ]\n");
	const ray r(point3(0, 0, 5), vec3(0, 0, -1));
	hit_record rec;
	ASSERT_TRUE(b.world->hit(r, interval(0.001, infinity), rec));
	EXPECT_NEAR(rec.t, 4.0, 1e-6) << "sanity check: this ray really lands on the pole";
	EXPECT_GT(rec.dpdv.length(), 0.5)
		<< "dpdv should have magnitude ~(thetaZMax-thetaZMin)*radius, not vanish";
}

TEST(PbrtCpuBuildTest, ClippedSphereSampleAreaLandsOnlyOnTheVisibleCap) {
	// zmin=0 keeps only the upper (z>=0) hemisphere. sample_area() is used
	// directly as a light EMISSION point (not re-intersected), so a sample
	// landing on the clipped-away z<0 region would be a real bias under
	// --bdpt/--sppm, not just wasted noise.
	//
	// b.lights (unlike b.world, which build() wraps in a bvh_node - see
	// pbrt_cpu_builder.h's own comment on that) stays a flat, unaccelerated
	// list, exactly matching how bdpt_adapter.h/sppm_adapter.h's own
	// real emitter-scan loops reach individual shapes' sample_area()
	// directly - so this needs a real AreaLightSource to land there at all.
	const pbrt_cpu::BuildResult b = buildFrom(
		"Material \"diffuse\" \"rgb reflectance\" [ 0 0 0 ]\n"
		"AreaLightSource \"diffuse\" \"rgb L\" [ 1 1 1 ]\n"
		"Shape \"sphere\" \"float radius\" [ 1 ] \"float zmin\" [ 0 ]\n");
	ASSERT_EQ(b.lights->objects.size(), 1u);
	for (double u1 = 0.0; u1 <= 1.0; u1 += 0.1) {
		for (double u2 = 0.0; u2 <= 1.0; u2 += 0.1) {
			AreaLightSample s;
			ASSERT_TRUE(b.lights->objects[0]->sample_area(u1, u2, s));
			EXPECT_GE(s.p.z(), -1e-9)
				<< "u1=" << u1 << " u2=" << u2 << " landed outside the kept z>=0 hemisphere";
		}
	}
	AreaLightSample lo, hi;
	ASSERT_TRUE(b.lights->objects[0]->sample_area(0.0, 0.0, lo));
	ASSERT_TRUE(b.lights->objects[0]->sample_area(1.0, 0.0, hi));
	EXPECT_NEAR(lo.p.z(), 0.0, 1e-6) << "u1=0 should land at zmin";
	EXPECT_NEAR(hi.p.z(), 1.0, 1e-6) << "u1=1 should land at zmax";
}

TEST(PbrtCpuBuildTest, ClippedSphereWithMediumInterfaceSkipsTheMediumWrapperButStaysReachable) {
	// Like MediumInterfaceWrapsTheSphereInAParticipatingMedium below, the
	// precise structural claim (s.medium stays the real index for GPU;
	// s.cpuMediumUnsupported gates CPU's own wrapper) lives in
	// pbrt_flatten_tests.cpp - this only confirms pbrt_cpu_builder.h's
	// cpuMediumUnsupported check doesn't skip building/registering the
	// clipped sphere itself, just its constant_medium wrapper.
	const pbrt_cpu::BuildResult b = buildFrom(
		"MakeNamedMedium \"fog\" \"string type\" \"homogeneous\"\n"
		"  \"rgb sigma_a\" [ 0.1 0.1 0.1 ] \"rgb sigma_s\" [ 2 2 2 ]\n"
		"AttributeBegin\n"
		"  MediumInterface \"fog\" \"\"\n"
		"  Shape \"sphere\" \"float radius\" [ 1 ] \"float zmin\" [ 0 ]\n"
		"AttributeEnd\n");
	EXPECT_EQ(b.sphereCount, 1u);
	double t = 0.0;
	EXPECT_TRUE(castRay(b, point3(0, 0, -5), vec3(0, 0, 1), t))
		<< "the clipped sphere itself must still be there, medium wrapper or not";
}

TEST(PbrtCpuBuildTest, DiskIsBuiltAtItsTransformedPositionAndRadius) {
	const pbrt_cpu::BuildResult b = buildFrom(
		"Translate 0 0 10\n"
		"Scale 2 2 2\n"
		"Shape \"disk\" \"float radius\" [ 1 ]\n");
	EXPECT_EQ(b.diskCount, 1u);
	double t = 0.0;
	// A disk at object-space z=0, radius 1, scaled by 2 and moved to z=10:
	// world-space disk of radius 2 centred at (0,0,10), still facing +Z.
	ASSERT_TRUE(castRay(b, point3(0, 0, 0), vec3(0, 0, 1), t));
	EXPECT_NEAR(t, 10.0, 1e-6);
}

TEST(PbrtCpuBuildTest, CylinderIsBuiltAtItsTransformedPositionAndRadius) {
	const pbrt_cpu::BuildResult b = buildFrom(
		"Translate 10 0 0\n"
		"Shape \"cylinder\" \"float radius\" [ 1 ] \"float zmin\" [ -1 ] "
		"\"float zmax\" [ 1 ]\n");
	EXPECT_EQ(b.cylinderCount, 1u);
	double t = 0.0;
	// The cylinder's axis (object-space Z) moves to run through x=10, y=0.
	// A ray fired along +X hits the near wall at x=10-radius=9.
	ASSERT_TRUE(castRay(b, point3(0, 0, 0), vec3(1, 0, 0), t));
	EXPECT_NEAR(t, 9.0, 1e-6);
}

TEST(PbrtCpuBuildTest, CylinderObjectMotionBlurMovesWithRayTime) {
	// Object motion blur (pbrt-v4 ActiveTransform "StartTime"/"EndTime") for
	// disk/cylinder - see pbrt_flatten::Disk::xformEnd's own comment and
	// disk_cylinder_hittable.h's header comment for the AnimatedTransform
	// mechanism this exercises end-to-end.
	const pbrt_cpu::BuildResult b = buildFrom(
		"ActiveTransform \"StartTime\"\n"
		"ActiveTransform \"EndTime\"\n"
		"Translate 5 0 0\n"
		"ActiveTransform \"All\"\n"
		"Shape \"cylinder\" \"float radius\" [ 1 ] \"float zmin\" [ -1 ] "
		"\"float zmax\" [ 1 ]\n");
	EXPECT_EQ(b.cylinderCount, 1u);

	{
		hit_record rec;
		const ray r(point3(-5, 0, 0), vec3(1, 0, 0), 0.0);
		ASSERT_TRUE(b.world->hit(r, interval(0.001, infinity), rec))
			<< "time=0 should hit the cylinder at its StartTime position (axis at x=0)";
		EXPECT_NEAR(rec.t, 4.0, 1e-6);
	}
	{
		hit_record rec;
		const ray r(point3(-5, 0, 0), vec3(1, 0, 0), 1.0);
		ASSERT_TRUE(b.world->hit(r, interval(0.001, infinity), rec))
			<< "time=1 should hit the cylinder at its EndTime position (axis at x=5)";
		EXPECT_NEAR(rec.t, 9.0, 1e-6);
	}
}

TEST(PbrtCpuBuildTest, CylinderRotatedObjectMotionBlurMovesWithRayTime) {
	// A Rotate baked into BOTH keyframes (a real .pbrt authoring idiom),
	// rather than pure translation - closes the gap the translation-only
	// test above leaves about whether a rotated keyframe pair is handled
	// correctly for cylinder specifically (disk's own rotation handling is
	// covered by DiskObjectMotionBlurMovesWithRayTime below).
	const pbrt_cpu::BuildResult b = buildFrom(
		"ActiveTransform \"StartTime\"\n"
		"Translate -6 -1.5 0\n"
		"Rotate 90 1 0 0\n"
		"ActiveTransform \"EndTime\"\n"
		"Translate 0 -1.5 0\n"
		"Rotate 90 1 0 0\n"
		"ActiveTransform \"All\"\n"
		"Shape \"cylinder\" \"float radius\" [ 0.6 ] \"float zmin\" [ -1 ] "
		"\"float zmax\" [ 1 ]\n");
	EXPECT_EQ(b.cylinderCount, 1u);

	// Rotate 90 about X maps the cylinder's object-space Z axis to world Y,
	// so the tube's lateral wall now runs vertically at world (x, [-2.5,
	// -0.5], 0) with radius 0.6 in the XZ plane - fire straight down +Z at
	// y=-1.5 (mid-tube) and the given x, hitting the near wall at z=-0.6.
	{
		hit_record rec;
		const ray r(point3(-6, -1.5, -5), vec3(0, 0, 1), 0.0);
		ASSERT_TRUE(b.world->hit(r, interval(0.001, infinity), rec))
			<< "time=0 should hit the cylinder at its StartTime position (axis at x=-6)";
		EXPECT_NEAR(rec.t, 4.4, 1e-6);
	}
	{
		hit_record rec;
		const ray r(point3(0, -1.5, -5), vec3(0, 0, 1), 1.0);
		ASSERT_TRUE(b.world->hit(r, interval(0.001, infinity), rec))
			<< "time=1 should hit the cylinder at its EndTime position (axis at x=0)";
		EXPECT_NEAR(rec.t, 4.4, 1e-6);
	}
	// Cross-check: at time=0, the EndTime position (x=0) must NOT be hit -
	// proves the shape genuinely moved rather than always covering both.
	{
		hit_record rec;
		const ray r(point3(0, -1.5, -5), vec3(0, 0, 1), 0.0);
		EXPECT_FALSE(b.world->hit(r, interval(0.001, infinity), rec))
			<< "at time=0 the cylinder should still be at x=-6, not covering x=0 too";
	}
}

TEST(PbrtCpuBuildTest, DiskGrowingFromDegenerateStartTimeStillRendersLater) {
	// A code-review pass on the commit that first added Disk/Cylinder object
	// motion blur found that gating the WHOLE animated shape's validity on
	// only the StartTime transform's invertibility (the same check a purely
	// static shape needs) made a legitimate "grow from nothing" animation -
	// ActiveTransform StartTime / Scale 0 0 0 / ... / ActiveTransform
	// EndTime / a normal transform - permanently invisible at every ray
	// time, not just near StartTime where the shape genuinely has zero
	// extent. This fires a ray at time=1.0 (EndTime, a perfectly
	// well-formed transform) and expects a real hit.
	const pbrt_cpu::BuildResult b = buildFrom(
		"ActiveTransform \"StartTime\"\n"
		"Scale 0 0 0\n"
		"ActiveTransform \"EndTime\"\n"
		"ActiveTransform \"All\"\n"
		"Shape \"disk\" \"float radius\" [ 1 ]\n");
	EXPECT_EQ(b.diskCount, 1u);

	hit_record rec;
	const ray r(point3(0, 0, -5), vec3(0, 0, 1), 1.0);
	ASSERT_TRUE(b.world->hit(r, interval(0.001, infinity), rec))
		<< "a shape animated from a degenerate StartTime must still render "
		   "at a later time whose own interpolated transform is well-formed";
	EXPECT_NEAR(rec.t, 5.0, 1e-6);
}

TEST(PbrtCpuBuildTest, RotatingDiskBoundingBoxCoversTheMidRotationSweep) {
	// A code-review pass on the commit that first added Disk/Cylinder object
	// motion blur found that unioning only the two ENDPOINT bounding boxes
	// underestimates the true swept volume under rotation: a disk rotating
	// from 0 to 180 degrees about the X axis starts AND ends flat in the
	// world XY plane (z-extent exactly 0 both times), but at the midpoint
	// (90 degrees) it stands up flat in the world XZ plane instead, with a
	// z-extent equal to its own full radius - matching DiskUnderRotation
	// HitsAtTheExactTransformedPosition's own established "Rotate 90 about
	// X swaps local Y/Z into world Y/Z" geometry, just at the ANIMATED
	// midpoint instead of a static rotation. A ray that only ever crosses
	// z=0.9 (inside the disk's radius=1, but well outside the buggy
	// zero-width z-extent a 2-box union would report) must still hit.
	const pbrt_cpu::BuildResult b = buildFrom(
		"ActiveTransform \"StartTime\"\n"
		"ActiveTransform \"EndTime\"\n"
		"Rotate 180 1 0 0\n"
		"ActiveTransform \"All\"\n"
		"Shape \"disk\" \"float radius\" [ 1 ]\n");
	EXPECT_EQ(b.diskCount, 1u);

	hit_record rec;
	// Local point (x=0, y=0.9, z=0) is within the unit disk and, under a 90
	// degree rotation about X (this scene's exact midpoint, t=0.5, since a
	// single-axis rotation slerps at constant angular velocity), lands on
	// the world Z axis at |z|=0.9 - try both signs, since the rotation
	// convention's sign isn't this test's concern, only that SOME point out
	// at |z|=0.9 is reachable at all.
	const ray rPos(point3(0, -5, 0.9), vec3(0, 1, 0), 0.5);
	const ray rNeg(point3(0, -5, -0.9), vec3(0, 1, 0), 0.5);
	const bool hitPos = b.world->hit(rPos, interval(0.001, infinity), rec);
	const bool hitNeg = !hitPos && b.world->hit(rNeg, interval(0.001, infinity), rec);
	ASSERT_TRUE(hitPos || hitNeg)
		<< "the disk's mid-rotation sweep (world |z|~0.9) must be reachable, "
		   "not culled by a bounding box built from only the two endpoints";
	EXPECT_NEAR(rec.t, 5.0, 1e-6);
}

TEST(PbrtCpuBuildTest, CylinderHasNoEndCaps) {
	// pbrt's cylinder (and this project's CylinderShape<T> port) is an open
	// tube, not a capped can - matching DiskShape/CylinderShape's own
	// intersect() which only solves for the lateral surface. A ray travelling
	// straight down the axis must pass through untouched.
	const pbrt_cpu::BuildResult b = buildFrom(
		"Shape \"cylinder\" \"float radius\" [ 1 ] \"float zmin\" [ -1 ] "
		"\"float zmax\" [ 1 ]\n");
	double t = 0.0;
	EXPECT_FALSE(castRay(b, point3(0, 0, -5), vec3(0, 0, 1), t))
		<< "a ray down the cylinder's own axis should exit through the open "
		   "end, not bounce off a cap that doesn't exist";
}

TEST(PbrtCpuBuildTest, DiskUnderRotationHitsAtTheExactTransformedPosition) {
	// Disk/Cylinder deliberately keep their CTM unbaked (see
	// disk_cylinder_hittable.h's own header comment) instead of following
	// Sphere's "bake to world-space centre+radius, warn under anisotropic
	// scale" approximation - Sphere can get away with that because it's
	// rotation-invariant, but a disk's orientation is exactly what a rotation
	// changes. This proves a rotated disk is hit exactly, not approximated.
	//
	// Rotating 90 degrees about X leaves the local X axis fixed and swaps
	// local Y and Z, so a disk originally spanning the object-space XY plane
	// (its outward normal along +/-Z) ends up spanning the world-space XZ
	// plane instead - regardless of the handedness convention for the
	// rotation's sign, since X is untouched either way.
	const pbrt_cpu::BuildResult b =
		buildFrom("Rotate 90 1 0 0\nShape \"disk\" \"float radius\" [ 1 ]\n");
	EXPECT_EQ(b.diskCount, 1u);
	double t = 0.0;
	// Local point (x=0.5, z=0) is within the unit disk (radius 0.5 < 1) and
	// keeps x=0.5 under the rotation, landing in the world-space y=0 plane.
	ASSERT_TRUE(castRay(b, point3(0.5, -5, 0), vec3(0, 1, 0), t));
	EXPECT_NEAR(t, 5.0, 1e-6);
}

TEST(PbrtCpuBuildTest, DiskObjectMotionBlurMovesWithRayTime) {
	// Object motion blur (pbrt-v4 ActiveTransform "StartTime"/"EndTime") for
	// disk - see CylinderObjectMotionBlurMovesWithRayTime's own comment for
	// the mechanism this exercises; same idiom, disk instead of cylinder.
	const pbrt_cpu::BuildResult b = buildFrom(
		"ActiveTransform \"StartTime\"\n"
		"ActiveTransform \"EndTime\"\n"
		"Translate 5 0 0\n"
		"ActiveTransform \"All\"\n"
		"Shape \"disk\" \"float radius\" [ 1 ]\n");
	EXPECT_EQ(b.diskCount, 1u);

	{
		hit_record rec;
		const ray r(point3(0, 0, -5), vec3(0, 0, 1), 0.0);
		ASSERT_TRUE(b.world->hit(r, interval(0.001, infinity), rec))
			<< "time=0 should hit the disk at its StartTime position (centre at x=0)";
		EXPECT_NEAR(rec.t, 5.0, 1e-6);
	}
	{
		hit_record rec;
		const ray r(point3(5, 0, -5), vec3(0, 0, 1), 1.0);
		ASSERT_TRUE(b.world->hit(r, interval(0.001, infinity), rec))
			<< "time=1 should hit the disk at its EndTime position (centre at x=5)";
		EXPECT_NEAR(rec.t, 5.0, 1e-6);
	}
	{
		hit_record rec;
		const ray r(point3(5, 0, -5), vec3(0, 0, 1), 0.0);
		EXPECT_FALSE(b.world->hit(r, interval(0.001, infinity), rec))
			<< "at time=0 the disk should still be at x=0, not covering x=5 too";
	}
}

TEST(PbrtCpuBuildTest, CurveIsBuiltAtItsTransformedPositionAndWidth) {
	// A straight cylinder-type curve along world-space Z, width0=width1=0.2
	// (a "thickened 1D curve" - like pbrt-v4's own Curve, CurveShape's hit
	// distance is where the ray crosses the curve's own centerline, not a
	// true swept-cylinder surface offset by radius; width only gates the
	// transverse-distance ACCEPTANCE threshold - see CurveShape::intersect's
	// hitWidth/halfW comment, shapes.h). This mirrors curve_shape_tests.cpp's
	// own FlatHitCentre/FlatMissOffset tests (t = exact axis-crossing
	// distance; width changes hit/miss at an offset, not t itself).
	const pbrt_cpu::BuildResult b = buildFrom(
		"Translate 3 0 0\n"
		"Shape \"curve\" \"string type\" [ \"cylinder\" ] \"float width\" [ 0.2 ]\n"
		"  \"point3 P\" [ 0 0 0  0 0 1  0 0 2  0 0 3 ]\n");
	EXPECT_EQ(b.curveCount, 1u);
	double t = 0.0;
	// Curve's world-space axis runs through x=3, y=0; a ray from x=-2 toward
	// +X aimed exactly at the axis (y=0) hits at the axis-crossing distance.
	ASSERT_TRUE(castRay(b, point3(-2, 0, 1.5), vec3(1, 0, 0), t));
	EXPECT_NEAR(t, 5.0, 1e-3);

	// A ray offset by less than the half-width (0.1) still hits...
	ASSERT_TRUE(castRay(b, point3(-2, 0.05, 1.5), vec3(1, 0, 0), t));
	// ...but one offset well past the half-width misses - proving the
	// authored width (not some other default) is what's actually reaching
	// CurveShape, the one thing the axis-crossing hit above doesn't prove.
	EXPECT_FALSE(castRay(b, point3(-2, 0.5, 1.5), vec3(1, 0, 0), t));
}

TEST(PbrtCpuBuildTest, CurveWithInvalidControlPointCountIsNotBuilt) {
	const pbrt_cpu::BuildResult b = buildFrom(
		"Shape \"curve\"\n"
		"  \"point3 P\" [ 0 0 0  1 1 0  2 1 0  3 0 0  4 0 0 ]\n");
	EXPECT_EQ(b.curveCount, 0u);
}

TEST(PbrtCpuBuildTest, HairMaterialSphereIsReachable) {
	// hair_material's scatter() is stochastic (Marschner lobe sampling) and
	// can reject a sample, so this only confirms the built sphere is hit
	// geometrically - the same "did it build and can a ray find it" bar
	// CloudMediumIsReachable/UniformgridMediumIsReachable use below. The
	// closed-form sigma_a math itself (direct/eumelanin-pheomelanin/default)
	// is covered by pbrt_flatten_tests.cpp's own Hair tests.
	const pbrt_cpu::BuildResult b = buildFrom(
		"Material \"hair\" \"float eumelanin\" [ 1.0 ] \"float pheomelanin\" [ 0.3 ]\n"
		"Shape \"sphere\" \"float radius\" [ 1 ]\n");
	double t = 0.0;
	ASSERT_TRUE(castRay(b, point3(0, 0, -5), vec3(0, 0, 1), t));
	EXPECT_NEAR(t, 4.0, 1e-6);
}

TEST(PbrtCpuBuildTest, MediumInterfaceWrapsTheSphereInAParticipatingMedium) {
	// The precise structural claim - the sphere resolves to the right
	// FlatScene::media index, with the right coefficients - lives in
	// pbrt_flatten_tests.cpp, which can check it before pbrt_cpu_builder.h's
	// own final BVH-folding step collapses every top-level hittable (sphere,
	// constant_medium wrapper, everything else) into a single bvh_node,
	// making world->objects.size() always 1 regardless of scene content and
	// unusable as a "how many things got added" signal here. This test only
	// confirms the CPU builder actually consumes that index without
	// crashing and the geometry stays reachable.
	const pbrt_cpu::BuildResult b = buildFrom(
		"MakeNamedMedium \"fog\" \"string type\" \"homogeneous\"\n"
		"  \"rgb sigma_a\" [ 0.1 0.1 0.1 ] \"rgb sigma_s\" [ 2 2 2 ]\n"
		"AttributeBegin\n"
		"  MediumInterface \"fog\" \"\"\n"
		"  Shape \"sphere\" \"float radius\" [ 1 ]\n"
		"AttributeEnd\n");
	EXPECT_EQ(b.sphereCount, 1u);

	// constant_medium stochastically decides whether a ray scatters inside
	// the volume, so this doesn't assert a specific outcome - only that the
	// medium-wrapped sphere is still reachable at all, the same "did it
	// build and can a ray find it" bar castRay() checks for plain geometry.
	double t = 0.0;
	EXPECT_TRUE(castRay(b, point3(0, 0, -5), vec3(0, 0, 1), t))
		<< "a ray toward the medium-wrapped sphere should still hit something";
}

TEST(PbrtCpuBuildTest, CameraMediumIsBuiltFromTheResolvedIndex) {
	const pbrt_cpu::BuildResult b = buildFrom(
		"MakeNamedMedium \"haze\" \"string type\" \"homogeneous\"\n"
		"  \"rgb sigma_a\" [ 0.01 0.01 0.01 ] \"rgb sigma_s\" [ 0.04 0.04 0.04 ]\n"
		"MediumInterface \"\" \"haze\"\n"
		"Camera \"perspective\" \"float fov\" [ 40 ]\n"
		"WorldBegin\n"
		"Shape \"sphere\" \"float radius\" [ 1 ]\n");
	ASSERT_TRUE(b.cameraMedium != nullptr);
	// sample_scatter() should eventually intercept a ray traveling toward
	// infinity (no real surface in front of it) - sigma_t=0.05 makes this
	// happen with overwhelming probability well before t=1e6.
	ray r(point3(0, 0, 0), vec3(0, 0, -1));
	hit_record rec;
	EXPECT_TRUE(b.cameraMedium->sample_scatter(r, 1e6, rec))
		<< "an unbounded medium with real extinction should (almost) always "
		   "scatter a long ray before it escapes";
}

TEST(PbrtCpuBuildTest, NoCameraMediumWithoutMediumInterfaceBeforeCamera) {
	const pbrt_cpu::BuildResult b = buildFrom(
		"Camera \"perspective\" \"float fov\" [ 40 ]\n"
		"WorldBegin\n"
		"Shape \"sphere\" \"float radius\" [ 1 ]\n");
	EXPECT_TRUE(b.cameraMedium == nullptr);
}

TEST(PbrtCpuBuildTest, EmissiveMediumWrapsTheSphereAndStaysReachable) {
	// "rgb Le"/"float Lescale" - same "builds, geometry stays reachable"
	// bar as MediumInterfaceWrapsTheSphereInAParticipatingMedium above; the
	// precise Le/Lescale resolution lives in pbrt_flatten_tests.cpp.
	const pbrt_cpu::BuildResult b = buildFrom(
		"MakeNamedMedium \"fire\" \"string type\" \"homogeneous\"\n"
		"  \"rgb sigma_a\" [ 0.5 0.5 0.5 ] \"rgb sigma_s\" [ 0.5 0.5 0.5 ]\n"
		"  \"rgb Le\" [ 5 2 0.5 ]\n"
		"AttributeBegin\n"
		"  MediumInterface \"fire\" \"\"\n"
		"  Shape \"sphere\" \"float radius\" [ 1 ]\n"
		"AttributeEnd\n");
	EXPECT_EQ(b.sphereCount, 1u);

	double t = 0.0;
	EXPECT_TRUE(castRay(b, point3(0, 0, -5), vec3(0, 0, 1), t))
		<< "a ray toward the emissive-medium-wrapped sphere should still hit something";
}

TEST(PbrtCpuBuildTest, EmissiveMediumBakesLeWeightedBySigmaAOverSigmaT) {
	// Unlike the reachability test above, this deterministically verifies
	// the ACTUAL emission value addMediumIfPresent (pbrt_cpu_builder.h)
	// computes and bakes into hg_phase_material - not just that something
	// built. sigma_a/sigma_s are astronomically large (not a realistic
	// scene value) so free_path (Beer-Lambert-sampled) is essentially
	// always far shorter than the sphere's own diameter - constant_medium's
	// stochastic collision test (hit(), constant_medium.h) resolves to a
	// real collision on effectively every ray, sidestepping the
	// "stochastic, don't assert specifics" caveat every other medium test
	// in this file carries (P(no collision) = exp(-sigma_t*diameter) =
	// exp(-2000) - not just unlikely, unrepresentable in a double).
	// Expected: sigma_a=800, sigma_s=200 (sigma_t=1000), Le=[5,2,0.5],
	// Lescale=1 -> emission = Le * (sigma_a/sigma_t) = Le * 0.8
	// = [4, 1.6, 0.4].
	// Material "none" (Interface) is essential here, not cosmetic - with no
	// Material directive the sphere gets pbrt's own default (opaque)
	// surface material, and the ray would hit THAT solid boundary first
	// (rec.mat = the default surface material, emitted() = black) rather
	// than ever reaching the medium's own internal stochastic collision -
	// exactly the scene-authoring pitfall documented in docs/PBRT_SUPPORT.md
	// (a medium boundary material must be near-invisible, not opaque).
	const pbrt_cpu::BuildResult b = buildFrom(
		"MakeNamedMedium \"fire\" \"string type\" \"homogeneous\"\n"
		"  \"rgb sigma_a\" [ 800 800 800 ] \"rgb sigma_s\" [ 200 200 200 ]\n"
		"  \"rgb Le\" [ 5 2 0.5 ]\n"
		"AttributeBegin\n"
		"  Material \"none\"\n"
		"  MediumInterface \"fire\" \"\"\n"
		"  Shape \"sphere\" \"float radius\" [ 1 ]\n"
		"AttributeEnd\n");
	ASSERT_EQ(b.sphereCount, 1u);

	// addMediumIfPresent (pbrt_cpu_builder.h) unconditionally adds BOTH the
	// raw sphere (its own hittable, carrying the Interface material) AND a
	// SEPARATE constant_medium wrapping the same sphere as its boundary -
	// so a single hit() call always finds the raw sphere's own (closer, at
	// the boundary entry t) surface first, same as the real path tracer's
	// first bounce would. The real integrator (camera.h) recognizes
	// is_medium_boundary() and CONTINUES the same ray from just past that
	// point for its next bounce, which is when it actually reaches the
	// medium's own internal stochastic collision - mirrored here with a
	// second hit() call starting just past the first hit, rather than
	// asserting on the first hit_record directly.
	const ray r(point3(0, 0, -5), vec3(0, 0, 1));
	hit_record entryRec;
	ASSERT_TRUE(b.world->hit(r, interval(0.001, infinity), entryRec))
		<< "a ray toward the emissive-medium-wrapped sphere should hit its boundary";
	ASSERT_TRUE(entryRec.mat != nullptr);
	ASSERT_TRUE(entryRec.mat->is_medium_boundary())
		<< "the boundary hit should be the Interface material, not an opaque surface";

	hit_record rec;
	ASSERT_TRUE(b.world->hit(r, interval(entryRec.t + 1e-4, infinity), rec))
		<< "continuing past the boundary should reach the medium's own internal collision";
	ASSERT_TRUE(rec.mat != nullptr);

	const color Le = rec.mat->emitted(r, rec, rec.u, rec.v, rec.p);
	EXPECT_NEAR(Le.x(), 4.0, 1e-9);
	EXPECT_NEAR(Le.y(), 1.6, 1e-9);
	EXPECT_NEAR(Le.z(), 0.4, 1e-9);
}

TEST(PbrtCpuBuildTest, UnknownMediumInterfaceNameIsTreatedAsVacuum) {
	// No MakeNamedMedium declares "ghost" - the parser should warn and fall
	// back to insideMedium=-1 (vacuum) rather than crash or misindex. See
	// FlattenTest.UnresolvedMediumNameIsVacuumAndWarns for the precise
	// index-level check; this only confirms the CPU builder still produces
	// an ordinary, reachable sphere rather than failing to build at all.
	const pbrt_cpu::BuildResult b = buildFrom(
		"AttributeBegin\n"
		"  MediumInterface \"ghost\" \"\"\n"
		"  Shape \"sphere\" \"float radius\" [ 1 ]\n"
		"AttributeEnd\n");
	EXPECT_EQ(b.sphereCount, 1u);
	double t = 0.0;
	EXPECT_TRUE(castRay(b, point3(0, 0, -5), vec3(0, 0, 1), t));
}

TEST(PbrtCpuBuildTest, CloudMediumIsReachable) {
	// Like MediumInterfaceWrapsTheSphereInAParticipatingMedium above, this
	// only confirms the standalone cloud_medium_hittable (added independently
	// of the declaring sphere - see addMediumIfPresent's own comment) builds
	// without crashing and stays reachable; CloudMedium's own density math is
	// covered by cloud_medium_hittable_tests.cpp / cloud_medium_tests.cpp.
	const pbrt_cpu::BuildResult b = buildFrom(
		"AttributeBegin\n"
		"  Scale 2 2 2\n"
		"  MakeNamedMedium \"puff\" \"string type\" [ \"cloud\" ]\n"
		"    \"rgb sigma_s\" [ 5 5 5 ]\n"
		"AttributeEnd\n"
		"AttributeBegin\n"
		"  Translate 1 1 1\n"
		"  MediumInterface \"puff\" \"\"\n"
		"  Shape \"sphere\" \"float radius\" [ 2 ]\n"
		"AttributeEnd\n");
	EXPECT_EQ(b.sphereCount, 1u);
	double t = 0.0;
	EXPECT_TRUE(castRay(b, point3(1, 1, -5), vec3(0, 0, 1), t))
		<< "a ray toward the cloud medium's world AABB should still hit something";
}

TEST(PbrtCpuBuildTest, RgbGridMediumIsReachable) {
	const pbrt_cpu::BuildResult b = buildFrom(
		"AttributeBegin\n"
		"  Scale 2 2 2\n"
		"  MakeNamedMedium \"nebula\" \"string type\" [ \"rgbgrid\" ]\n"
		"    \"integer nx\" [ 2 ] \"integer ny\" [ 1 ] \"integer nz\" [ 1 ]\n"
		"    \"rgb sigma_s\" [ 1 1 1  2 2 2 ]\n"
		"AttributeEnd\n"
		"AttributeBegin\n"
		"  Translate 1 1 1\n"
		"  MediumInterface \"nebula\" \"\"\n"
		"  Shape \"sphere\" \"float radius\" [ 2 ]\n"
		"AttributeEnd\n");
	EXPECT_EQ(b.sphereCount, 1u);
	double t = 0.0;
	EXPECT_TRUE(castRay(b, point3(1, 1, -5), vec3(0, 0, 1), t))
		<< "a ray toward the rgbgrid medium's world AABB should still hit something";
}

TEST(PbrtCpuBuildTest, UniformgridMediumIsReachable) {
	const pbrt_cpu::BuildResult b = buildFrom(
		"AttributeBegin\n"
		"  Scale 2 2 2\n"
		"  MakeNamedMedium \"fog\" \"string type\" [ \"uniformgrid\" ]\n"
		"    \"integer nx\" [ 2 ] \"integer ny\" [ 1 ] \"integer nz\" [ 1 ]\n"
		"    \"float density\" [ 0.5 1.0 ]\n"
		"AttributeEnd\n"
		"AttributeBegin\n"
		"  Translate 1 1 1\n"
		"  MediumInterface \"fog\" \"\"\n"
		"  Shape \"sphere\" \"float radius\" [ 2 ]\n"
		"AttributeEnd\n");
	EXPECT_EQ(b.sphereCount, 1u);
	double t = 0.0;
	EXPECT_TRUE(castRay(b, point3(1, 1, -5), vec3(0, 0, 1), t))
		<< "a ray toward the uniformgrid medium's world AABB should still hit something";
}

TEST(PbrtCpuBuildTest, NanoVdbMediumIsReachable) {
	// pbrt_scenes/nanovdb-sphere.nvdb - a small synthetic fog-volume sphere
	// (radius 1.5, centered at its own local origin) authored via NanoVDB's
	// own tools/CreatePrimitives.h, not read from any external asset this
	// repo doesn't already bundle. buildFrom() bypasses pbrt_load.h's path
	// resolution (its own comment on other tests), so the filename must
	// already resolve from the test binary's own working directory - the
	// repo root (see this project's own build-toolchain notes).
	const pbrt_cpu::BuildResult b = buildFrom(
		"MakeNamedMedium \"fog\" \"string type\" [ \"nanovdb\" ] "
		"\"string filename\" [ \"pbrt_scenes/nanovdb-sphere.nvdb\" ]\n"
		"AttributeBegin\n"
		"  MediumInterface \"fog\" \"\"\n"
		"  Shape \"sphere\" \"float radius\" [ 3 ]\n"
		"AttributeEnd\n");
	EXPECT_EQ(b.sphereCount, 1u);
	double t = 0.0;
	EXPECT_TRUE(castRay(b, point3(0, 0, -5), vec3(0, 0, 1), t))
		<< "a ray straight through the nanovdb grid's own dense centre "
		   "(world origin) should hit something - either the boundary "
		   "sphere itself or a real scatter event inside the medium";
}

TEST(PbrtCpuBuildTest, NanoVdbTemperatureNameProducesRealEmission) {
	// pbrt_scenes/nanovdb-fire-test.nvdb - a small (4x4x4) synthetic asset
	// with BOTH a "density" grid (uniform 0.5) and a "temperature" grid
	// (uniform 3000K, a warm orange), authored the same way nanovdb-sphere.
	// nvdb was (NanoVDB's own header-only tools::build::Grid, no external
	// asset download). Exercises the real end-to-end file-reading path
	// (pbrt_cpu_builder.h's addMediumIfPresent nanovdb branch reading a
	// SECOND named grid) that neither pbrt_flatten_tests.cpp (string-parsing
	// only, no file I/O) nor sampled_grid_tests.cpp (GridMediumData::
	// set_emission() fed hand-built in-memory vectors, no NanoVDB reader
	// involved) actually cover.
	const pbrt_cpu::BuildResult b = buildFrom(
		"MakeNamedMedium \"fire\" \"string type\" [ \"nanovdb\" ] "
		"\"string filename\" [ \"pbrt_scenes/nanovdb-fire-test.nvdb\" ] "
		"\"rgb sigma_a\" [ 0.5 0.5 0.5 ] \"rgb sigma_s\" [ 0.1 0.1 0.1 ] "
		"\"string temperaturename\" [ \"temperature\" ]\n"
		"AttributeBegin\n"
		// addMediumIfPresent adds the built grid_medium_hittable to `world`
		// directly (independent of this shape's own geometry/material) as
		// soon as ANY shape references the medium via MediumInterface - the
		// "trigger" shape's only real job is naming the medium, so it's put
		// far away and shrunk to nothing here rather than sized to actually
		// enclose the grid (as nanovdb-medium.pbrt's own convention does for
		// a real render): a raw world->hit() picks the SINGLE nearest
		// surface, so a boundary shape that genuinely wrapped the medium
		// would always be hit first (its dielectric material never emits),
		// masking the medium's own scatter/emission hits this test needs to
		// reach directly.
		"  Translate 1000 1000 1000\n"
		"  Material \"dielectric\" \"float eta\" [ 1.001 ]\n"
		"  MediumInterface \"fire\" \"\"\n"
		"  Shape \"sphere\" \"float radius\" [ 0.01 ]\n"
		"AttributeEnd\n");
	EXPECT_EQ(b.sphereCount, 1u);

	// The medium scatters/emits via stochastic delta-tracking (see
	// grid_medium_hittable.h's own march_segments()), so a single ray isn't
	// guaranteed to land on a real scatter event - fire many rays through
	// the grid's dense centre and confirm at least one produces a hit whose
	// material genuinely emits a nonzero, warm (3000K, this asset's own
	// uniform temperature) colour.
	bool sawEmission = false;
	for (int i = 0; i < 500 && !sawEmission; ++i) {
		hit_record rec;
		// The 4x4x4 grid's own native index space [0,4]^3 maps directly to
		// world space (no Translate applied at MakeNamedMedium time), so
		// its centre is (2,2,2), not the world origin - aim through that,
		// not (0,0,z).
		const ray r(point3(2, 2, -20), vec3(0, 0, 1));
		if (!b.world->hit(r, interval(0.001, infinity), rec)) continue;
		ASSERT_TRUE(rec.mat != nullptr);
		const color le = rec.mat->emitted(r, rec, rec.u, rec.v, rec.p);
		if (le.x() > 0.0 || le.y() > 0.0 || le.z() > 0.0) {
			sawEmission = true;
			EXPECT_GT(le.x(), le.z())
				<< "3000K is a warm colour - red should dominate blue";
		}
	}
	EXPECT_TRUE(sawEmission)
		<< "expected at least one real scatter event with nonzero blackbody "
		   "emission across 500 ray casts through the medium's dense centre";
}

// A heterogeneous medium's sigma is per world unit, but the camera hands the medium a ray whose direction is
// pixel_sample - origin, hundreds of units long. The grid, rgbgrid and cloud hittables sampled free flights along
// the raw ray parameter, so a primary ray saw a medium |d| times too thin and the CPU rendered them 4-10% too
// bright next to both GPU backends. The chance of scattering on the way through must not depend on |d|.
TEST(PbrtCpuBuildTest, UniformGridMediumThicknessDoesNotDependOnRayDirectionLength) {
	const pbrt_cpu::BuildResult b = buildFrom(
		"MakeNamedMedium \"g\" \"string type\" [ \"uniformgrid\" ] "
		"\"integer nx\" [ 2 ] \"integer ny\" [ 2 ] \"integer nz\" [ 2 ] "
		"\"float density\" [ 1 1 1 1 1 1 1 1 ] "
		"\"rgb sigma_a\" [ 0 0 0 ] \"rgb sigma_s\" [ 0.7 0.7 0.7 ]\n"
		"AttributeBegin\n"
		"  Translate 0.5 0.5 0.5\n"
		"  Material \"interface\"\n"
		"  MediumInterface \"g\" \"\"\n"
		"  Shape \"sphere\" \"float radius\" [ 1 ]\n"
		"AttributeEnd\n");
	const auto scatterFraction = [&](double dirLength) {
		int scattered = 0;
		constexpr int kRays = 4000;
		for (int i = 0; i < kRays; ++i) {
			hit_record rec;
			const ray r(point3(0.5, 0.5, 0.1), vec3(0, 0, dirLength));   // inside the sphere, so its own surface is not met; the ray ends where the cube does (and starts at 0: a minimum in ray-parameter units would skip |d| times as much)
			if (b.world->hit(r, interval(0.0, 0.9 / dirLength), rec)) {
				++scattered;
				EXPECT_GT(rec.t * dirLength, 0.0) << "rec.t is in ray-parameter units, so the hit is past the origin";
				EXPECT_LT(rec.t * dirLength, 0.9) << "and inside the unit cube, whatever the direction's length";
			}
		}
		return static_cast<double>(scattered) / kRays;
	};
	const double unit = scatterFraction(1.0);
	const double longDir = scatterFraction(300.0);
	EXPECT_GT(unit, 0.2) << "the medium should scatter a visible share of rays";
	EXPECT_NEAR(longDir, unit, 0.05);
}

TEST(PbrtCpuBuildTest, NanoVdbMissingFilenameBuildsNoMediumButKeepsTheShape) {
	const pbrt_cpu::BuildResult b = buildFrom(
		"MakeNamedMedium \"fog\" \"string type\" [ \"nanovdb\" ]\n"
		"AttributeBegin\n"
		"  MediumInterface \"fog\" \"\"\n"
		"  Shape \"sphere\" \"float radius\" [ 1 ]\n"
		"AttributeEnd\n");
	EXPECT_EQ(b.sphereCount, 1u)
		<< "the boundary sphere itself must still build even though the "
		   "medium (no filename given) doesn't";
	double t = 0.0;
	EXPECT_TRUE(castRay(b, point3(0, 0, -5), vec3(0, 0, 1), t))
		<< "the sphere's own opaque-by-default material still hits";
}

TEST(PbrtCpuBuildTest, SharedVerticesAreDeduplicated) {
	// The two triangles of a quad share two corners. FlatScene stores all six
	// vertices explicitly; the builder should recover the original four.
	const pbrt_cpu::BuildResult b = buildFrom(kQuad);
	EXPECT_EQ(b.uniqueVertexCount, 4u);
}

TEST(PbrtCpuBuildTest, DistinctVerticesAreNotCollapsed) {
	// Guards the dedup against being too eager: two separate triangles sharing
	// no corners must keep all six vertices.
	const pbrt_cpu::BuildResult b = buildFrom(
		"Shape \"trianglemesh\" \"integer indices\" [ 0 1 2 ]\n"
		"  \"point3 P\" [ 0 0 0  1 0 0  0 1 0 ]\n"
		"Shape \"trianglemesh\" \"integer indices\" [ 0 1 2 ]\n"
		"  \"point3 P\" [ 5 5 5  6 5 5  5 6 5 ]\n");
	EXPECT_EQ(b.triangleCount, 2u);
	EXPECT_EQ(b.uniqueVertexCount, 6u);
}

TEST(PbrtCpuBuildTest, EmissiveShapesGoIntoTheLightList) {
	const pbrt_cpu::BuildResult b = buildFrom(
		std::string("AttributeBegin\n"
					"  AreaLightSource \"diffuse\" \"rgb L\" [ 5 5 5 ]\n")
		+ kQuad + "AttributeEnd\n" + kQuad);
	EXPECT_EQ(b.triangleCount, 4u);
	EXPECT_EQ(b.lights->objects.size(), 2u)
		<< "only the triangles inside the attribute scope are emissive";
}

TEST(PbrtCpuBuildTest, AreaLightFilenameBuildsATextureBackedTwoSidedDiffuseLight) {
	// buildFrom() only runs flatten(), not pbrt_load.h's resolution pass (see
	// DiffuseReflectanceImagemapBuildsATextureBackedLambertian's own comment
	// on the same trade-off), so this only checks that a non-empty
	// Emission::filename makes it all the way to a texture-backed,
	// is_two_sided() diffuse_light - makeMaterial()'s own filename branch -
	// rather than the flat-L solid_color-backed one.
	const pbrt_cpu::BuildResult b = buildFrom(
		std::string("AttributeBegin\n"
					"  AreaLightSource \"diffuse\" \"string filename\" [ \"glow.png\" ]"
					" \"bool twosided\" [ true ]\n")
		+ kQuad + "AttributeEnd\n");
	hit_record rec;
	ASSERT_TRUE(b.world->hit(ray(point3(0.5, 0.5, -5), vec3(0, 0, 1)),
							 interval(0.001, infinity), rec));
	auto *dl = dynamic_cast<diffuse_light *>(rec.mat.get());
	ASSERT_NE(dl, nullptr);
	EXPECT_TRUE(dl->is_two_sided());
	EXPECT_EQ(dynamic_cast<mipmap_texture *>(dl->get_texture().get()), dl->get_texture().get())
		<< "a \"filename\" area light must build a mipmap_texture-backed "
		   "diffuse_light, not a flat solid_color one";
}

TEST(PbrtCpuBuildTest, AreaLightWithoutFilenameBuildsAPlainSolidColorDiffuseLight) {
	const pbrt_cpu::BuildResult b = buildFrom(
		std::string("AttributeBegin\n"
					"  AreaLightSource \"diffuse\" \"rgb L\" [ 5 5 5 ]\n")
		+ kQuad + "AttributeEnd\n");
	hit_record rec;
	ASSERT_TRUE(b.world->hit(ray(point3(0.5, 0.5, -5), vec3(0, 0, 1)),
							 interval(0.001, infinity), rec));
	auto *dl = dynamic_cast<diffuse_light *>(rec.mat.get());
	ASSERT_NE(dl, nullptr);
	EXPECT_FALSE(dl->is_two_sided());
	EXPECT_EQ(dynamic_cast<mipmap_texture *>(dl->get_texture().get()), nullptr);
}

TEST(PbrtCpuBuildTest, NonEmissiveSceneHasAnEmptyLightList) {
	const pbrt_cpu::BuildResult b = buildFrom(kQuad);
	EXPECT_TRUE(b.lights->objects.empty());
}

TEST(PbrtCpuBuildTest, AnEmptySceneBuildsWithoutCrashing) {
	// flatten() drops unsupported shapes, so a scene can legitimately arrive
	// with nothing in it. A BVH must not be built over zero primitives.
	// "hyperboloid" - not "cylinder"/"disk"/"cone"/"paraboloid", which this
	// loader now supports.
	const pbrt_cpu::BuildResult b = buildFrom("Shape \"hyperboloid\"\n");
	EXPECT_EQ(b.triangleCount, 0u);
	EXPECT_EQ(b.sphereCount, 0u);
	EXPECT_EQ(b.diskCount, 0u);
	EXPECT_EQ(b.cylinderCount, 0u);
	EXPECT_EQ(b.coneCount, 0u);
	EXPECT_EQ(b.paraboloidCount, 0u);
	ASSERT_NE(b.world, nullptr);
	EXPECT_TRUE(b.world->objects.empty());
}

TEST(PbrtCpuBuildTest, MaterialsAreSharedRatherThanCopiedPerPrimitive) {
	// A million-triangle mesh with one material should hold one material
	// object, not a million.
	const pbrt_cpu::BuildResult b = buildFrom(
		std::string("Material \"diffuse\" \"rgb reflectance\" [ .5 .5 .5 ]\n") + kQuad);
	ASSERT_EQ(b.triangleCount, 2u);

	// The world is BVH-wrapped, so reach the triangles by hitting them rather
	// than walking the object graph.
	hit_record a, c;
	ASSERT_TRUE(b.world->hit(ray(point3(0.25, 0.25, -5), vec3(0, 0, 1)),
							 interval(0.001, infinity), a));
	ASSERT_TRUE(b.world->hit(ray(point3(0.75, 0.75, -5), vec3(0, 0, 1)),
							 interval(0.001, infinity), c));
	EXPECT_EQ(a.mat.get(), c.mat.get());
}

// ---------------------------------------------------------------------------
// makeMaterial() used to fall through to lambertian for these three kinds
// silently - no warning, unlike the genuinely-unsupported case - even though
// coated_diffuse, coated_conductor and diffuse_transmission all already
// existed in material_pbrt.h. That made CPU and GPU render the same pbrt
// file with different materials for any scene using one of them. These pin
// the real class getting built, not just that something renders.
// ---------------------------------------------------------------------------

TEST(PbrtCpuBuildTest, CoatedDiffuseBuildsTheRealMaterialNotLambertian) {
	const pbrt_cpu::BuildResult b = buildFrom(
		"Material \"coateddiffuse\" \"rgb reflectance\" [ .2 .4 .6 ] "
		"\"float roughness\" [ .1 ]\n" + std::string(kQuad));
	hit_record rec;
	ASSERT_TRUE(b.world->hit(ray(point3(0.5, 0.5, -5), vec3(0, 0, 1)),
							 interval(0.001, infinity), rec));
	EXPECT_NE(dynamic_cast<coated_diffuse *>(rec.mat.get()), nullptr)
		<< "a pbrt coateddiffuse material must build the real coated_diffuse "
		   "class, not silently fall back to lambertian";
}

TEST(PbrtCpuBuildTest, CoatedConductorBuildsTheRealMaterialNotLambertian) {
	const pbrt_cpu::BuildResult b = buildFrom(
		"Material \"coatedconductor\" \"rgb reflectance\" [ .8 .8 .8 ]\n"
		+ std::string(kQuad));
	hit_record rec;
	ASSERT_TRUE(b.world->hit(ray(point3(0.5, 0.5, -5), vec3(0, 0, 1)),
							 interval(0.001, infinity), rec));
	EXPECT_NE(dynamic_cast<coated_conductor *>(rec.mat.get()), nullptr)
		<< "a pbrt coatedconductor material must build the real "
		   "coated_conductor class, not silently fall back to lambertian";
}

TEST(PbrtCpuBuildTest, DiffuseReflectanceImagemapBuildsATextureBackedLambertian) {
	// buildFrom() only runs flatten(), not pbrt_load.h's resolution pass, so
	// Material::textureFilename stays "as written" here ("t.png") rather than
	// an absolute path - fine for this test, which only checks that a
	// non-empty textureFilename makes it all the way to a mipmap_texture-
	// backed lambertian (this Diffuse case's own branch in makeMaterial()),
	// not a plain solid_color-backed one built from `reflectance`.
	const pbrt_cpu::BuildResult b = buildFrom(
		"Texture \"tmap\" \"spectrum\" \"imagemap\" \"string filename\" [ \"t.png\" ]\n"
		"Material \"diffuse\" \"texture reflectance\" [ \"tmap\" ]\n"
		+ std::string(kQuad));
	hit_record rec;
	ASSERT_TRUE(b.world->hit(ray(point3(0.5, 0.5, -5), vec3(0, 0, 1)),
							 interval(0.001, infinity), rec));
	auto *lam = dynamic_cast<lambertian *>(rec.mat.get());
	ASSERT_NE(lam, nullptr);
	EXPECT_NE(dynamic_cast<mipmap_texture *>(lam->get_texture().get()), nullptr)
		<< "a diffuse material with an imagemap-bound reflectance must build a "
		   "texture-backed lambertian, not the flat-colour fallback";
}

TEST(PbrtCpuBuildTest, CoatedDiffuseReflectanceImagemapBuildsATextureBackedCoatedDiffuse) {
	// pbrt's own ganesha scene's exact binding shape (a bare imagemap, no
	// wrapping "scale") - see pbrt_flatten::Material::textureFilename's own
	// comment on why CoatedDiffuse is now gated in alongside Diffuse.
	const pbrt_cpu::BuildResult b = buildFrom(
		"Texture \"tmap\" \"spectrum\" \"imagemap\" \"string filename\" [ \"t.png\" ]\n"
		"Material \"coateddiffuse\" \"texture reflectance\" [ \"tmap\" ]\n"
		+ std::string(kQuad));
	hit_record rec;
	ASSERT_TRUE(b.world->hit(ray(point3(0.5, 0.5, -5), vec3(0, 0, 1)),
							 interval(0.001, infinity), rec));
	auto *cd = dynamic_cast<coated_diffuse *>(rec.mat.get());
	ASSERT_NE(cd, nullptr);
	EXPECT_NE(dynamic_cast<mipmap_texture *>(cd->get_texture().get()), nullptr)
		<< "a coateddiffuse material with an imagemap-bound reflectance must build a "
		   "texture-backed coated_diffuse, not the flat-colour fallback";
}

TEST(PbrtCpuBuildTest, DielectricRoughnessImagemapBuildsATextureBackedRoughDielectric) {
	// pbrt-v4 "texture roughness" on a Dielectric bound to a bare
	// imagemap - see pbrt_flatten::Material::roughnessTextureFilename's
	// own comment. Must build a real texture-backed rough_dielectric (not
	// the flat-roughness overload, and not a plain smooth dielectric).
	const pbrt_cpu::BuildResult b = buildFrom(
		"Texture \"scratch\" \"float\" \"imagemap\" \"string filename\" [ \"scratch.png\" ]\n"
		"Material \"dielectric\" \"texture roughness\" [ \"scratch\" ]\n"
		+ std::string(kQuad));
	hit_record rec;
	ASSERT_TRUE(b.world->hit(ray(point3(0.5, 0.5, -5), vec3(0, 0, 1)),
							 interval(0.001, infinity), rec));
	auto *rd = dynamic_cast<rough_dielectric *>(rec.mat.get());
	ASSERT_NE(rd, nullptr);
	EXPECT_NE(dynamic_cast<mipmap_texture *>(rd->get_roughness_texture().get()), nullptr)
		<< "a dielectric material with an imagemap-bound roughness must build a "
		   "texture-backed rough_dielectric, not the flat-roughness/smooth fallback";
}

TEST(PbrtCpuBuildTest, CoatedDiffuseReflectanceScaleWrappedImagemapAppliesTheScale) {
	// barcelona-pavilion's own dominant binding shape (an imagemap wrapped
	// in a "scale" texture, e.g. materials.pbrt's "concrete-kd") - Material::
	// textureScale's own comment.
	const pbrt_cpu::BuildResult b = buildFrom(
		"Texture \"tmap\" \"spectrum\" \"imagemap\" \"string filename\" [ \"t.png\" ]\n"
		"Texture \"tmap-scaled\" \"spectrum\" \"scale\" \"texture tex\" [ \"tmap\" ] "
		"\"float scale\" [ 0.5 ]\n"
		"Material \"coateddiffuse\" \"texture reflectance\" [ \"tmap-scaled\" ]\n"
		+ std::string(kQuad));
	hit_record rec;
	ASSERT_TRUE(b.world->hit(ray(point3(0.5, 0.5, -5), vec3(0, 0, 1)),
							 interval(0.001, infinity), rec));
	auto *cd = dynamic_cast<coated_diffuse *>(rec.mat.get());
	ASSERT_NE(cd, nullptr);
	auto *scaled = dynamic_cast<scaled_texture *>(cd->get_texture().get());
	ASSERT_NE(scaled, nullptr)
		<< "a scale-wrapped imagemap reflectance must build a scaled_texture "
		   "wrapping the real mipmap_texture, not just the bare image";
}

TEST(PbrtCpuBuildTest, CoatedDiffuseReflectanceCheckerboardBuildsAUVCheckerBackedCoatedDiffuse) {
	// checkerboard/fbm/marble/mix used to be Diffuse-only; now also resolved
	// for CoatedDiffuse (mirrors DiffuseReflectanceCheckerboardBuildsA
	// UVCheckerBackedLambertian above, just via coated_diffuse's own
	// texture-taking constructor).
	const pbrt_cpu::BuildResult b = buildFrom(
		"Texture \"chk\" \"spectrum\" \"checkerboard\" "
		"\"rgb tex1\" [ 1 0 0 ] \"rgb tex2\" [ 0 0 1 ] "
		"\"float uscale\" [ 1 ] \"float vscale\" [ 1 ]\n"
		"Material \"coateddiffuse\" \"texture reflectance\" [ \"chk\" ]\n"
		+ std::string(kQuad));
	hit_record rec;
	ASSERT_TRUE(b.world->hit(ray(point3(0.25, 0.25, -5), vec3(0, 0, 1)),
							 interval(0.001, infinity), rec));
	auto *cd = dynamic_cast<coated_diffuse *>(rec.mat.get());
	ASSERT_NE(cd, nullptr);
	auto *chk = dynamic_cast<uv_checker_texture *>(cd->get_texture().get());
	ASSERT_NE(chk, nullptr)
		<< "a coateddiffuse material with a checkerboard-bound reflectance must "
		   "build a uv_checker_texture-backed coated_diffuse";
	const color c00 = chk->value(0.25, 0.25, rec.p);
	const color c10 = chk->value(1.25, 0.25, rec.p);
	EXPECT_NE(c00.x(), c10.x())
		<< "adjacent UV checker cells (one uscale apart) must differ";
}

TEST(PbrtCpuBuildTest, CoatedDiffuseReflectanceMixBuildsAMixBackedCoatedDiffuse) {
	const pbrt_cpu::BuildResult b = buildFrom(
		"Texture \"dirt\" \"spectrum\" \"mix\" \"rgb tex1\" [ 1 0 0 ] "
		"\"rgb tex2\" [ 0 0 1 ] \"float amount\" [ 0.25 ]\n"
		"Material \"coateddiffuse\" \"texture reflectance\" [ \"dirt\" ]\n"
		+ std::string(kQuad));
	hit_record rec;
	ASSERT_TRUE(b.world->hit(ray(point3(0.5, 0.5, -5), vec3(0, 0, 1)),
							 interval(0.001, infinity), rec));
	auto *cd = dynamic_cast<coated_diffuse *>(rec.mat.get());
	ASSERT_NE(cd, nullptr);
	auto *mix = dynamic_cast<mix_texture *>(cd->get_texture().get());
	ASSERT_NE(mix, nullptr)
		<< "a coateddiffuse material with a mix-bound reflectance must build a "
		   "mix_texture-backed coated_diffuse, not the flat-colour fallback";
}

TEST(PbrtCpuBuildTest, CoatedDiffuseReflectanceFbmBuildsAnFbmBackedCoatedDiffuse) {
	// Mirrors DiffuseReflectanceFbmBuildsAnFbmBackedLambertian below, for
	// CoatedDiffuse's own separate branch (previously untested - checkerboard
	// and mix were covered above, but fbm/marble were not).
	const pbrt_cpu::BuildResult b = buildFrom(
		"Texture \"cloud\" \"float\" \"fbm\" \"integer octaves\" [ 4 ]\n"
		"Material \"coateddiffuse\" \"texture reflectance\" [ \"cloud\" ]\n"
		+ std::string(kQuad));
	hit_record rec;
	ASSERT_TRUE(b.world->hit(ray(point3(0.5, 0.5, -5), vec3(0, 0, 1)),
							 interval(0.001, infinity), rec));
	auto *cd = dynamic_cast<coated_diffuse *>(rec.mat.get());
	ASSERT_NE(cd, nullptr);
	EXPECT_NE(dynamic_cast<fbm_texture *>(cd->get_texture().get()), nullptr)
		<< "a coateddiffuse material with an fbm-bound reflectance must build an "
		   "fbm_texture-backed coated_diffuse, not the flat-colour fallback";
}

TEST(PbrtCpuBuildTest, CoatedDiffuseReflectanceMarbleBuildsAMarbleBackedCoatedDiffuse) {
	// Mirrors DiffuseReflectanceMarbleBuildsAMarbleBackedLambertian below,
	// for CoatedDiffuse's own separate branch.
	const pbrt_cpu::BuildResult b = buildFrom(
		"Texture \"stone\" \"spectrum\" \"marble\"\n"
		"Material \"coateddiffuse\" \"texture reflectance\" [ \"stone\" ]\n"
		+ std::string(kQuad));
	hit_record rec;
	ASSERT_TRUE(b.world->hit(ray(point3(0.5, 0.5, -5), vec3(0, 0, 1)),
							 interval(0.001, infinity), rec));
	auto *cd = dynamic_cast<coated_diffuse *>(rec.mat.get());
	ASSERT_NE(cd, nullptr);
	EXPECT_NE(dynamic_cast<marble_texture *>(cd->get_texture().get()), nullptr)
		<< "a coateddiffuse material with a marble-bound reflectance must build a "
		   "marble_texture-backed coated_diffuse, not the flat-colour fallback";
}

TEST(PbrtCpuBuildTest, DiffuseReflectanceScaleWrappedImagemapAppliesTheScale) {
	// barcelona-pavilion's own dominant binding shape, now also supported for
	// plain diffuse surfaces (not just coateddiffuse) - Material::
	// textureScale's own comment.
	const pbrt_cpu::BuildResult b = buildFrom(
		"Texture \"tmap\" \"spectrum\" \"imagemap\" \"string filename\" [ \"t.png\" ]\n"
		"Texture \"tmap-scaled\" \"spectrum\" \"scale\" \"texture tex\" [ \"tmap\" ] "
		"\"float scale\" [ 0.5 ]\n"
		"Material \"diffuse\" \"texture reflectance\" [ \"tmap-scaled\" ]\n"
		+ std::string(kQuad));
	hit_record rec;
	ASSERT_TRUE(b.world->hit(ray(point3(0.5, 0.5, -5), vec3(0, 0, 1)),
							 interval(0.001, infinity), rec));
	auto *lam = dynamic_cast<lambertian *>(rec.mat.get());
	ASSERT_NE(lam, nullptr);
	auto *scaled = dynamic_cast<scaled_texture *>(lam->get_texture().get());
	ASSERT_NE(scaled, nullptr)
		<< "a scale-wrapped imagemap reflectance on a plain diffuse material must "
		   "build a scaled_texture wrapping the real mipmap_texture, not just the "
		   "bare image";
}

TEST(PbrtCpuBuildTest, DiffuseReflectanceCheckerboardBuildsAUVCheckerBackedLambertian) {
	// Round 5 Phase 2: hasCheckerReflectance must make it all the way to a
	// uv_checker_texture-backed lambertian, and that texture must actually
	// tile by UV (not the unrelated 3D world-space checker_texture) - probed
	// here via value() at two UVs one checker cell apart.
	const pbrt_cpu::BuildResult b = buildFrom(
		"Texture \"chk\" \"spectrum\" \"checkerboard\" "
		"\"rgb tex1\" [ 1 0 0 ] \"rgb tex2\" [ 0 0 1 ] "
		"\"float uscale\" [ 1 ] \"float vscale\" [ 1 ]\n"
		"Material \"diffuse\" \"texture reflectance\" [ \"chk\" ]\n"
		+ std::string(kQuad));
	hit_record rec;
	ASSERT_TRUE(b.world->hit(ray(point3(0.25, 0.25, -5), vec3(0, 0, 1)),
							 interval(0.001, infinity), rec));
	auto *lam = dynamic_cast<lambertian *>(rec.mat.get());
	ASSERT_NE(lam, nullptr);
	auto *chk = dynamic_cast<uv_checker_texture *>(lam->get_texture().get());
	ASSERT_NE(chk, nullptr)
		<< "a diffuse material with a checkerboard-bound reflectance must "
		   "build a uv_checker_texture-backed lambertian";
	const color c00 = chk->value(0.25, 0.25, rec.p);
	const color c10 = chk->value(1.25, 0.25, rec.p);
	EXPECT_NE(c00.x(), c10.x())
		<< "adjacent UV checker cells (one uscale apart) must differ";
}

TEST(PbrtCpuBuildTest, CheckerboardWithNestedImagemapTex1BuildsAMipmapBackedSlot) {
	// One-level-nested tex1 (bound to a bare imagemap Texture) must build a
	// REAL mipmap_texture for that slot, not silently fall back to reading
	// checkerColor1 (which stays at its unused default here). buildFrom()
	// skips pbrt_load.h's resolution pass, so "leaf.png" never actually
	// resolves/decodes - mipmap_texture's own documented "corrupt-or-missing
	// file -> cyan debug colour (0,1,1)" fallback (texture.h) is exactly the
	// signal this test uses to confirm a REAL mipmap_texture is in that
	// slot, as opposed to any flat colour (which could never coincidentally
	// equal solid cyan).
	const pbrt_cpu::BuildResult b = buildFrom(
		"Texture \"leaf\" \"spectrum\" \"imagemap\" \"string filename\" [ \"leaf.png\" ]\n"
		"Texture \"chk\" \"spectrum\" \"checkerboard\" \"texture tex1\" [ \"leaf\" ] "
		"\"rgb tex2\" [ 0 0 1 ] \"float uscale\" [ 1 ] \"float vscale\" [ 1 ]\n"
		"Material \"diffuse\" \"texture reflectance\" [ \"chk\" ]\n"
		+ std::string(kQuad));
	hit_record rec;
	ASSERT_TRUE(b.world->hit(ray(point3(0.25, 0.25, -5), vec3(0, 0, 1)),
							 interval(0.001, infinity), rec));
	auto *lam = dynamic_cast<lambertian *>(rec.mat.get());
	ASSERT_NE(lam, nullptr);
	auto *chk = dynamic_cast<uv_checker_texture *>(lam->get_texture().get());
	ASSERT_NE(chk, nullptr);
	const color tex1Cell = chk->value(0.25, 0.25, rec.p);   // (0,0) cell - tex1 (nested imagemap)
	const color tex2Cell = chk->value(1.25, 0.25, rec.p);   // (1,0) cell - tex2 (flat blue literal)
	EXPECT_NEAR(tex1Cell.x(), 0.0, 1e-9);
	EXPECT_NEAR(tex1Cell.y(), 1.0, 1e-9);
	EXPECT_NEAR(tex1Cell.z(), 1.0, 1e-9);
	EXPECT_NEAR(tex2Cell.z(), 1.0, 1e-9) << "tex2 must stay the flat blue literal, unaffected";
}

TEST(PbrtCpuBuildTest, CheckerboardWithSecondLevelNestedCheckerboardBuildsRealNestedTexture) {
	// A second level of nesting: the outer checkerboard's tex1 names ANOTHER
	// checkerboard Texture (itself nesting a bare imagemap in ITS tex1),
	// instead of a flat literal or a bare imagemap directly - real recursive
	// evaluation, not the flat-average-colour approximation GPU falls back
	// to (see pbrt_flatten::NestedProceduralTexture's own comment).
	const pbrt_cpu::BuildResult b = buildFrom(
		"Texture \"leaf\" \"spectrum\" \"imagemap\" \"string filename\" [ \"leaf.png\" ]\n"
		"Texture \"inner\" \"spectrum\" \"checkerboard\" \"texture tex1\" [ \"leaf\" ] "
		"\"rgb tex2\" [ 0 1 0 ] \"float uscale\" [ 1 ] \"float vscale\" [ 1 ]\n"
		"Texture \"outer\" \"spectrum\" \"checkerboard\" \"texture tex1\" [ \"inner\" ] "
		"\"rgb tex2\" [ 0 0 1 ] \"float uscale\" [ 1 ] \"float vscale\" [ 1 ]\n"
		"Material \"diffuse\" \"texture reflectance\" [ \"outer\" ]\n"
		+ std::string(kQuad));
	hit_record rec;
	ASSERT_TRUE(b.world->hit(ray(point3(0.25, 0.25, -5), vec3(0, 0, 1)),
							 interval(0.001, infinity), rec));
	auto *lam = dynamic_cast<lambertian *>(rec.mat.get());
	ASSERT_NE(lam, nullptr);
	auto *outer = dynamic_cast<uv_checker_texture *>(lam->get_texture().get());
	ASSERT_NE(outer, nullptr);
	auto *inner = dynamic_cast<uv_checker_texture *>(outer->get_tex1().get());
	ASSERT_NE(inner, nullptr)
		<< "outer checkerboard's tex1 slot must build a REAL nested "
		   "uv_checker_texture, not fall back to a flat colour";
	// Outer's (0,0) cell is tex1 (the inner checkerboard); at (0.25,0.25) the
	// inner pattern's own (0,0) cell is ITS tex1 - the imagemap, unresolved
	// here so it reads back as mipmap_texture's cyan (0,1,1) debug fallback.
	const color outerTex1Cell = outer->value(0.25, 0.25, rec.p);
	EXPECT_NEAR(outerTex1Cell.x(), 0.0, 1e-9);
	EXPECT_NEAR(outerTex1Cell.y(), 1.0, 1e-9);
	EXPECT_NEAR(outerTex1Cell.z(), 1.0, 1e-9);
	// Outer's (1,0) cell is tex2 (flat blue), unaffected by the nesting.
	const color outerTex2Cell = outer->value(1.25, 0.25, rec.p);
	EXPECT_NEAR(outerTex2Cell.z(), 1.0, 1e-9);
	// Sample the inner checkerboard directly (not through outer) to confirm
	// its own tex2 (flat green) is intact and independently addressable.
	const color innerTex2Cell = inner->value(1.25, 0.25, rec.p);
	EXPECT_NEAR(innerTex2Cell.y(), 1.0, 1e-9);
	EXPECT_NEAR(innerTex2Cell.x(), 0.0, 1e-9);
}

TEST(PbrtCpuBuildTest, MixWithSecondLevelNestedMixBuildsRealNestedTexture) {
	const pbrt_cpu::BuildResult b = buildFrom(
		"Texture \"inner\" \"spectrum\" \"mix\" \"rgb tex1\" [ 1 0 0 ] "
		"\"rgb tex2\" [ 0 1 0 ] \"float amount\" [ 1 ]\n"
		"Texture \"outer\" \"spectrum\" \"mix\" \"texture tex1\" [ \"inner\" ] "
		"\"rgb tex2\" [ 0 0 1 ] \"float amount\" [ 0 ]\n"
		"Material \"diffuse\" \"texture reflectance\" [ \"outer\" ]\n"
		+ std::string(kQuad));
	hit_record rec;
	ASSERT_TRUE(b.world->hit(ray(point3(0.5, 0.5, -5), vec3(0, 0, 1)),
							 interval(0.001, infinity), rec));
	auto *lam = dynamic_cast<lambertian *>(rec.mat.get());
	ASSERT_NE(lam, nullptr);
	auto *outer = dynamic_cast<mix_texture *>(lam->get_texture().get());
	ASSERT_NE(outer, nullptr);
	auto *inner = dynamic_cast<mix_texture *>(outer->get_tex1().get());
	ASSERT_NE(inner, nullptr)
		<< "outer mix's tex1 slot must build a REAL nested mix_texture, not "
		   "fall back to a flat colour";
	// Outer amount=0 -> pure tex1 -> inner amount=1 -> pure inner tex2 (green).
	const color result = outer->value(0.5, 0.5, rec.p);
	EXPECT_NEAR(result.x(), 0.0, 1e-9);
	EXPECT_NEAR(result.y(), 1.0, 1e-9);
	EXPECT_NEAR(result.z(), 0.0, 1e-9);
}

TEST(PbrtCpuBuildTest, DiffuseReflectanceFbmBuildsAnFbmBackedLambertian) {
	// Round 6 Phase 1: hasFbmReflectance must make it all the way to an
	// fbm_texture-backed lambertian, reusing the existing CPU class rather
	// than falling back to a flat colour.
	const pbrt_cpu::BuildResult b = buildFrom(
		"Texture \"cloud\" \"float\" \"fbm\" \"integer octaves\" [ 4 ]\n"
		"Material \"diffuse\" \"texture reflectance\" [ \"cloud\" ]\n"
		+ std::string(kQuad));
	hit_record rec;
	ASSERT_TRUE(b.world->hit(ray(point3(0.5, 0.5, -5), vec3(0, 0, 1)),
							 interval(0.001, infinity), rec));
	auto *lam = dynamic_cast<lambertian *>(rec.mat.get());
	ASSERT_NE(lam, nullptr);
	EXPECT_NE(dynamic_cast<fbm_texture *>(lam->get_texture().get()), nullptr)
		<< "a diffuse material with an fbm-bound reflectance must build an "
		   "fbm_texture-backed lambertian, not the flat-colour fallback";
}

TEST(PbrtCpuBuildTest, DiffuseReflectanceMarbleBuildsAMarbleBackedLambertian) {
	// Round 6 Phase 1: hasMarbleReflectance must make it all the way to a
	// marble_texture-backed lambertian.
	const pbrt_cpu::BuildResult b = buildFrom(
		"Texture \"stone\" \"spectrum\" \"marble\"\n"
		"Material \"diffuse\" \"texture reflectance\" [ \"stone\" ]\n"
		+ std::string(kQuad));
	hit_record rec;
	ASSERT_TRUE(b.world->hit(ray(point3(0.5, 0.5, -5), vec3(0, 0, 1)),
							 interval(0.001, infinity), rec));
	auto *lam = dynamic_cast<lambertian *>(rec.mat.get());
	ASSERT_NE(lam, nullptr);
	EXPECT_NE(dynamic_cast<marble_texture *>(lam->get_texture().get()), nullptr)
		<< "a diffuse material with a marble-bound reflectance must build a "
		   "marble_texture-backed lambertian, not the flat-colour fallback";
}

TEST(PbrtCpuBuildTest, DiffuseReflectanceMixBuildsAMixBackedLambertian) {
	// Round 6 Phase 1: hasMixReflectance must make it all the way to a
	// mix_texture-backed lambertian that actually blends tex1/tex2 by
	// amount (deterministic, unlike fbm/marble - so the resulting colour
	// can be checked exactly).
	const pbrt_cpu::BuildResult b = buildFrom(
		"Texture \"dirt\" \"spectrum\" \"mix\" \"rgb tex1\" [ 1 0 0 ] "
		"\"rgb tex2\" [ 0 0 1 ] \"float amount\" [ 0.25 ]\n"
		"Material \"diffuse\" \"texture reflectance\" [ \"dirt\" ]\n"
		+ std::string(kQuad));
	hit_record rec;
	ASSERT_TRUE(b.world->hit(ray(point3(0.5, 0.5, -5), vec3(0, 0, 1)),
							 interval(0.001, infinity), rec));
	auto *lam = dynamic_cast<lambertian *>(rec.mat.get());
	ASSERT_NE(lam, nullptr);
	auto *mix = dynamic_cast<mix_texture *>(lam->get_texture().get());
	ASSERT_NE(mix, nullptr)
		<< "a diffuse material with a mix-bound reflectance must build a "
		   "mix_texture-backed lambertian, not the flat-colour fallback";
	const color c = mix->value(0.0, 0.0, rec.p);
	EXPECT_NEAR(c.x(), 0.75, 1e-9);  // (1-0.25)*1 + 0.25*0
	EXPECT_NEAR(c.z(), 0.25, 1e-9);  // (1-0.25)*0 + 0.25*1
}

TEST(PbrtCpuBuildTest, DiffuseReflectanceMixAmountImagemapBuildsATextureBackedBlend) {
	// pbrt-v4's real "amount" bound to a Texture (e.g. an fbm-driven dirt/
	// wear mask) - previously unsupported, now resolved the same one-level
	// nested-bare-imagemap way as tex1/tex2 (Material::
	// mixAmountTextureFilename's own comment). Must build a REAL
	// mipmap_texture-backed amount, not the flat-scalar fallback.
	const pbrt_cpu::BuildResult b = buildFrom(
		"Texture \"wear\" \"float\" \"imagemap\" \"string filename\" [ \"wear.png\" ]\n"
		"Texture \"dirt\" \"spectrum\" \"mix\" \"rgb tex1\" [ 1 0 0 ] "
		"\"rgb tex2\" [ 0 0 1 ] \"texture amount\" [ \"wear\" ]\n"
		"Material \"diffuse\" \"texture reflectance\" [ \"dirt\" ]\n"
		+ std::string(kQuad));
	hit_record rec;
	ASSERT_TRUE(b.world->hit(ray(point3(0.5, 0.5, -5), vec3(0, 0, 1)),
							 interval(0.001, infinity), rec));
	auto *lam = dynamic_cast<lambertian *>(rec.mat.get());
	ASSERT_NE(lam, nullptr);
	auto *mix = dynamic_cast<mix_texture *>(lam->get_texture().get());
	ASSERT_NE(mix, nullptr);
	EXPECT_NE(dynamic_cast<mipmap_texture *>(mix->get_amount_texture().get()), nullptr)
		<< "a mix reflectance with a texture-bound amount must build a "
		   "texture-backed blend fraction, not the flat-scalar fallback";
}

TEST(PbrtCpuBuildTest, CoatedDiffuseReflectanceMixAmountImagemapBuildsATextureBackedBlend) {
	// Same as DiffuseReflectanceMixAmountImagemapBuildsATextureBackedBlend
	// above, for CoatedDiffuse's own separate branch.
	const pbrt_cpu::BuildResult b = buildFrom(
		"Texture \"wear\" \"float\" \"imagemap\" \"string filename\" [ \"wear.png\" ]\n"
		"Texture \"dirt\" \"spectrum\" \"mix\" \"rgb tex1\" [ 1 0 0 ] "
		"\"rgb tex2\" [ 0 0 1 ] \"texture amount\" [ \"wear\" ]\n"
		"Material \"coateddiffuse\" \"texture reflectance\" [ \"dirt\" ]\n"
		+ std::string(kQuad));
	hit_record rec;
	ASSERT_TRUE(b.world->hit(ray(point3(0.5, 0.5, -5), vec3(0, 0, 1)),
							 interval(0.001, infinity), rec));
	auto *cd = dynamic_cast<coated_diffuse *>(rec.mat.get());
	ASSERT_NE(cd, nullptr);
	auto *mix = dynamic_cast<mix_texture *>(cd->get_texture().get());
	ASSERT_NE(mix, nullptr);
	EXPECT_NE(dynamic_cast<mipmap_texture *>(mix->get_amount_texture().get()), nullptr)
		<< "a mix reflectance with a texture-bound amount must build a "
		   "texture-backed blend fraction, not the flat-scalar fallback";
}

TEST(PbrtCpuBuildTest, DiffuseReflectanceMixAmountCheckerboardBuildsARealNestedBlend) {
	// The new second-level nesting (Material::mixAmountNested's own comment)
	// - "amount" bound to a further checkerboard Texture, not just a bare
	// imagemap. Must build a real uv_checker_texture-backed amount (via
	// checkerOrMixSlot), not the flat-scalar (nestedProceduralAverageScalar)
	// fallback GPU alone uses.
	const pbrt_cpu::BuildResult b = buildFrom(
		"Texture \"chk\" \"spectrum\" \"checkerboard\"\n"
		"Texture \"dirt\" \"spectrum\" \"mix\" \"rgb tex1\" [ 1 0 0 ] "
		"\"rgb tex2\" [ 0 0 1 ] \"texture amount\" [ \"chk\" ]\n"
		"Material \"diffuse\" \"texture reflectance\" [ \"dirt\" ]\n"
		+ std::string(kQuad));
	hit_record rec;
	ASSERT_TRUE(b.world->hit(ray(point3(0.5, 0.5, -5), vec3(0, 0, 1)),
							 interval(0.001, infinity), rec));
	auto *lam = dynamic_cast<lambertian *>(rec.mat.get());
	ASSERT_NE(lam, nullptr);
	auto *mix = dynamic_cast<mix_texture *>(lam->get_texture().get());
	ASSERT_NE(mix, nullptr);
	EXPECT_NE(dynamic_cast<uv_checker_texture *>(mix->get_amount_texture().get()), nullptr)
		<< "a mix reflectance with a checkerboard-bound amount must build a "
		   "real second-level nested texture, not the flat-scalar fallback";
}

TEST(PbrtCpuBuildTest, CoatedDiffuseReflectanceMixAmountCheckerboardBuildsARealNestedBlend) {
	// Same as DiffuseReflectanceMixAmountCheckerboardBuildsARealNestedBlend
	// above, but for CoatedDiffuse's own hand-duplicated copy of the
	// hasMixReflectance/mixAmountNested block (pbrt_cpu_builder.h's
	// CoatedDiffuse case) - the two call sites aren't shared, so a
	// divergence between them would otherwise go untested.
	const pbrt_cpu::BuildResult b = buildFrom(
		"Texture \"chk\" \"spectrum\" \"checkerboard\"\n"
		"Texture \"dirt\" \"spectrum\" \"mix\" \"rgb tex1\" [ 1 0 0 ] "
		"\"rgb tex2\" [ 0 0 1 ] \"texture amount\" [ \"chk\" ]\n"
		"Material \"coateddiffuse\" \"texture reflectance\" [ \"dirt\" ]\n"
		+ std::string(kQuad));
	hit_record rec;
	ASSERT_TRUE(b.world->hit(ray(point3(0.5, 0.5, -5), vec3(0, 0, 1)),
							 interval(0.001, infinity), rec));
	auto *coated = dynamic_cast<coated_diffuse *>(rec.mat.get());
	ASSERT_NE(coated, nullptr);
	auto *mix = dynamic_cast<mix_texture *>(coated->get_texture().get());
	ASSERT_NE(mix, nullptr);
	EXPECT_NE(dynamic_cast<uv_checker_texture *>(mix->get_amount_texture().get()), nullptr)
		<< "a coateddiffuse mix reflectance with a checkerboard-bound amount "
		   "must build a real second-level nested texture, not the "
		   "flat-scalar fallback";
}
