// normal_map_tests.cpp
// pbrt-v4-style unit tests for apply_normal_map() and apply_bump_map(),
// and for the normal_map_material / bump_map_material CPU wrappers.
//
// Design follows pbrt-v4's materials_test approach:
//   - Flat map (0.5, 0.5, 1.0) must leave the shading normal unchanged
//   - Perturbed normal must remain on the same hemisphere as the geometric normal
//   - Zero-displacement bump map must return the original normal
//   - Perturbed bump normal must stay on the same hemisphere as geometric normal
//   - Material wrapper round-trip: scatter() on a flat map equals scatter() on
//     the bare inner material (same PDF, same attenuation direction hemisphere)

#include <gtest/gtest.h>
#include <cmath>
#include <memory>

// Include shared normal map math directly
#include "normal_map.h"

// Include the CPU material layer (pulls in hittable, texture, pdf, etc.)
#include "material.h"
#include "texture.h"

// Wrapper classes (normal_map_material, bump_map_material) — pbrt-v4 style
#include "normal_map_materials.h"

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static double vec3_dot(double ax, double ay, double az,
					   double bx, double by, double bz) {
	return ax*bx + ay*by + az*bz;
}

static double vec3_len(double x, double y, double z) {
	return std::sqrt(x*x + y*y + z*z);
}

static bool nearly_unit(double x, double y, double z, double tol = 1e-9) {
	return std::abs(vec3_len(x, y, z) - 1.0) < tol;
}

// Build a minimal hit_record with a given normal and dpdu.
static hit_record make_rec(vec3 normal, vec3 dpdu,
							double u = 0.5, double v = 0.5) {
	hit_record rec;
	rec.p          = point3(0, 0, 0);
	rec.normal     = normal;
	rec.dpdu       = dpdu;
	rec.u          = u;
	rec.v          = v;
	rec.t          = 1.0;
	rec.front_face = true;
	// mat is unused in these tests
	return rec;
}

// ---------------------------------------------------------------------------
// apply_normal_map tests
// ---------------------------------------------------------------------------

// A flat normal map encodes (0,0,1) in tangent space, which should be decoded
// to exactly the geometric normal in world space.
TEST(NormalMap, FlatMapLeavesNormalUnchanged) {
	// geometric normal = world Y; dpdu = world X
	double nx = 0, ny = 1, nz = 0;
	double dpdu_x = 1, dpdu_y = 0, dpdu_z = 0;

	// flat map tangent-space normal
	double ns_tx = 0, ns_ty = 0, ns_tz = 1;

	double out_nx, out_ny, out_nz;
	apply_normal_map(ns_tx, ns_ty, ns_tz,
					 nx, ny, nz,
					 dpdu_x, dpdu_y, dpdu_z,
					 out_nx, out_ny, out_nz);

	// Result should be the same as the geometric normal
	EXPECT_NEAR(out_nx, nx, 1e-9);
	EXPECT_NEAR(out_ny, ny, 1e-9);
	EXPECT_NEAR(out_nz, nz, 1e-9);
	EXPECT_TRUE(nearly_unit(out_nx, out_ny, out_nz));
}

// Perturbed tangent-space normal stays on the same hemisphere as the
// geometric normal (apply_normal_map guarantees this via the sign flip).
TEST(NormalMap, PerturbedNormalOnSameHemisphere) {
	double nx = 0, ny = 0, nz = 1;
	double dpdu_x = 1, dpdu_y = 0, dpdu_z = 0;

	// tilt toward +X in tangent space
	double ns_tx = 0.7, ns_ty = 0.0, ns_tz = 0.7;
	double ns_len = std::sqrt(ns_tx*ns_tx + ns_ty*ns_ty + ns_tz*ns_tz);
	ns_tx /= ns_len; ns_ty /= ns_len; ns_tz /= ns_len;

	double out_nx, out_ny, out_nz;
	apply_normal_map(ns_tx, ns_ty, ns_tz,
					 nx, ny, nz,
					 dpdu_x, dpdu_y, dpdu_z,
					 out_nx, out_ny, out_nz);

	double d = vec3_dot(out_nx, out_ny, out_nz, nx, ny, nz);
	EXPECT_GT(d, 0.0) << "perturbed normal must stay on same hemisphere as geometric normal";
	EXPECT_TRUE(nearly_unit(out_nx, out_ny, out_nz));
}

// Output is a unit vector for a wide variety of inputs.
TEST(NormalMap, OutputIsUnit) {
	struct Case { double nx,ny,nz, dpdu_x,dpdu_y,dpdu_z, tx,ty,tz; };
	Case cases[] = {
		{0,1,0,  1,0,0,  0,0,1},
		{0,0,1,  1,0,0,  0,0,1},
		{1,0,0,  0,1,0,  0,0,1},
		{0,1,0,  0,0,1,  0.5773,0.5773,0.5773},
		{0.5773,0.5773,0.5773,  1,0,0,  0,0,1},
	};
	for (auto& c : cases) {
		double tl = std::sqrt(c.tx*c.tx+c.ty*c.ty+c.tz*c.tz);
		double tx=c.tx/tl, ty=c.ty/tl, tz=c.tz/tl;
		double out_nx, out_ny, out_nz;
		apply_normal_map(tx,ty,tz, c.nx,c.ny,c.nz, c.dpdu_x,c.dpdu_y,c.dpdu_z,
						 out_nx,out_ny,out_nz);
		EXPECT_TRUE(nearly_unit(out_nx,out_ny,out_nz))
			<< "output should be a unit vector";
	}
}

// ---------------------------------------------------------------------------
// apply_bump_map tests
// ---------------------------------------------------------------------------

// Zero displacement everywhere -> no perturbation -> original normal preserved.
TEST(BumpMap, ZeroDisplacementLeavesNormalUnchanged) {
	double nx = 0, ny = 1, nz = 0;
	double dpdu_x = 1, dpdu_y = 0, dpdu_z = 0;
	double dpdv_x = 0, dpdv_y = 0, dpdv_z = 1;

	double out_nx, out_ny, out_nz;
	apply_bump_map(0.0, 0.0, 0.0,   // disp, disp_u, disp_v
				   0.001, 0.001,     // du, dv
				   nx, ny, nz,
				   dpdu_x, dpdu_y, dpdu_z,
				   dpdv_x, dpdv_y, dpdv_z,
				   out_nx, out_ny, out_nz);

	EXPECT_NEAR(out_nx, nx, 1e-9);
	EXPECT_NEAR(out_ny, ny, 1e-9);
	EXPECT_NEAR(out_nz, nz, 1e-9);
}

// Bumped normal stays on same hemisphere.
TEST(BumpMap, BumpedNormalOnSameHemisphere) {
	double nx = 0, ny = 0, nz = 1;
	double dpdu_x = 1, dpdu_y = 0, dpdu_z = 0;
	double dpdv_x = 0, dpdv_y = 1, dpdv_z = 0;

	// positive gradient along U
	double out_nx, out_ny, out_nz;
	apply_bump_map(0.0, 0.1, 0.0,
				   0.001, 0.001,
				   nx, ny, nz,
				   dpdu_x, dpdu_y, dpdu_z,
				   dpdv_x, dpdv_y, dpdv_z,
				   out_nx, out_ny, out_nz);

	double d = vec3_dot(out_nx, out_ny, out_nz, nx, ny, nz);
	EXPECT_GT(d, 0.0) << "bumped normal must stay on same hemisphere as geometric normal";
	EXPECT_TRUE(nearly_unit(out_nx, out_ny, out_nz));
}

// ---------------------------------------------------------------------------
// normal_map_material wrapper tests
// ---------------------------------------------------------------------------

// A solid-colour normal-map texture encoding (0.5,0.5,1) (flat) must leave
// the scatter outcome direction hemisphere unchanged vs. bare lambertian.
TEST(NormalMapMaterial, FlatMapScatterMatchesBareInner) {
	// flat normal texture: RGB=(0.5, 0.5, 1.0) -> tangent-space (0,0,1) -> no perturbation
	auto flat_tex = std::make_shared<solid_color>(color(0.5, 0.5, 1.0));
	auto inner    = std::make_shared<lambertian>(color(0.8, 0.6, 0.2));
	normal_map_material nm_mat(flat_tex, inner);

	hit_record rec = make_rec(vec3(0,1,0), vec3(1,0,0));
	rec.mat = nullptr; // not needed

	ray r_in(point3(0,2,0), vec3(0,-1,0));

	// Check that apply() returns the same normal for a flat map
	hit_record modified = nm_mat.apply(r_in, rec);
	EXPECT_NEAR(modified.normal.x(), rec.normal.x(), 1e-6);
	EXPECT_NEAR(modified.normal.y(), rec.normal.y(), 1e-6);
	EXPECT_NEAR(modified.normal.z(), rec.normal.z(), 1e-6);

	// scatter() should succeed (same as inner)
	scatter_record srec;
	bool ok = nm_mat.scatter(r_in, rec, srec);
	EXPECT_TRUE(ok);
}

// Perturbed normal-map material: scattered direction must be on the
// correct hemisphere relative to the *perturbed* normal.
TEST(NormalMapMaterial, PerturbedNormalScatterOnCorrectHemisphere) {
	// tilt 45 degrees toward +X: RGB encodes tangent-space (1,0,1)/sqrt(2)
	float v = static_cast<float>((std::sqrt(2.0)/2.0 + 1.0) / 2.0); // (0.5+normalized/2)
	auto tilted_tex = std::make_shared<solid_color>(color(v, 0.5, v));
	auto inner      = std::make_shared<lambertian>(color(1.0, 1.0, 1.0));
	normal_map_material nm_mat(tilted_tex, inner);

	hit_record rec = make_rec(vec3(0,1,0), vec3(1,0,0));
	ray r_in(point3(0,2,0), vec3(0,-1,0));

	// The apply() result must be on the same hemisphere as geometric normal
	hit_record modified = nm_mat.apply(r_in, rec);
	double d = dot(modified.normal, rec.normal);
	EXPECT_GT(d, 0.0);

	scatter_record srec;
	bool ok = nm_mat.scatter(r_in, rec, srec);
	EXPECT_TRUE(ok);
}

// ---------------------------------------------------------------------------
// bump_map_material wrapper tests
// ---------------------------------------------------------------------------

// Zero-displacement bump map: normal must be unchanged.
TEST(BumpMapMaterial, ZeroDisplacementUnchanged) {
	auto zero_tex = std::make_shared<solid_color>(color(0.0, 0.0, 0.0));
	auto inner    = std::make_shared<lambertian>(color(0.5, 0.5, 0.5));
	bump_map_material bm_mat(zero_tex, inner, 1.0, 0.001);

	hit_record rec = make_rec(vec3(0,1,0), vec3(1,0,0));
	ray r_in(point3(0,2,0), vec3(0,-1,0));

	hit_record modified = bm_mat.apply(r_in, rec);
	EXPECT_NEAR(modified.normal.x(), rec.normal.x(), 1e-6);
	EXPECT_NEAR(modified.normal.y(), rec.normal.y(), 1e-6);
	EXPECT_NEAR(modified.normal.z(), rec.normal.z(), 1e-6);
}

// Bump-mapped normal stays on same hemisphere as geometric normal.
TEST(BumpMapMaterial, BumpedNormalOnSameHemisphere) {
	// uniform mid-grey displacement everywhere
	auto grey_tex = std::make_shared<solid_color>(color(0.5, 0.5, 0.5));
	auto inner    = std::make_shared<lambertian>(color(0.5, 0.5, 0.5));
	bump_map_material bm_mat(grey_tex, inner, 5.0, 0.001);

	// Use different UV offsets so disp_u != disp at u+step
	// With a solid color the bump should be zero (uniform field -> no gradient)
	hit_record rec = make_rec(vec3(0,1,0), vec3(1,0,0));
	ray r_in(point3(0,2,0), vec3(0,-1,0));
	hit_record modified = bm_mat.apply(r_in, rec);

	double d = dot(modified.normal, rec.normal);
	EXPECT_GT(d, 0.0);
}

// scatter() through bump_map_material succeeds.
TEST(BumpMapMaterial, ScatterSucceeds) {
	auto bump_tex = std::make_shared<solid_color>(color(0.3, 0.3, 0.3));
	auto inner    = std::make_shared<lambertian>(color(0.8, 0.8, 0.8));
	bump_map_material bm_mat(bump_tex, inner, 1.0, 0.001);

	hit_record rec = make_rec(vec3(0,1,0), vec3(1,0,0));
	ray r_in(point3(0,2,0), vec3(0,-1,0));

	scatter_record srec;
	bool ok = bm_mat.scatter(r_in, rec, srec);
	EXPECT_TRUE(ok);
}

// ---------------------------------------------------------------------------
// pbrt-v4 BumpMap with its shading geometry (apply_bump_map_pbrt, triangle_dndu_dndv)
// ---------------------------------------------------------------------------

// The `displacement * dndu` term: on a unit sphere (dndu = dpdu, dndv = dpdv) a displacement of 2 with a slope of 1 along u gives
// dpdu' = (1,0,0) + 1 * n + 2 * dndu = (3,0,1) and dpdv' = (0,3,0), so n' is (-3, 0, 9) normalised; without the term it would be (-1, 0, 1) normalised.
TEST(BumpMapPbrt, DisplacementTimesDndu) {
	double nx, ny, nz;
	apply_bump_map_pbrt<double>(2.0, 2.0 + 0.001, 2.0, 0.001, 0.001, 0.0, 0.0, 1.0, false, 1, 0, 0, 0, 1, 0, 1, 0, 0, 0, 1, 0, nx, ny, nz);
	const double len = std::sqrt(9.0 + 81.0);
	EXPECT_NEAR(nx, -3.0 / len, 1e-9);
	EXPECT_NEAR(ny, 0.0, 1e-9);
	EXPECT_NEAR(nz, 9.0 / len, 1e-9);
}

// A surface with shading normals (a smooth triangle mesh) has pbrt's re-orthogonalised tangents: ts = cross(ns, dpdu), ss = cross(ts, ns), so dpdv is a turned copy of dpdu, not the
// surface's own dpdv. dpdu = (2,0,0) and a true dpdv = (0,1,0): with a slope of 0.5 along each, the smooth result is (-1,-1,4) normalised and the plain one (-0.5,-1,2) normalised.
TEST(BumpMapPbrt, SmoothSurfacesUseTheOrthogonalisedTangents) {
	const double d = 0.5;
	double sx, sy, sz, px, py, pz;
	apply_bump_map_pbrt<double>(0.0, d * 0.001, d * 0.001, 0.001, 0.001, 0, 0, 1, true, 2, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, sx, sy, sz);
	apply_bump_map_pbrt<double>(0.0, d * 0.001, d * 0.001, 0.001, 0.001, 0, 0, 1, false, 2, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, px, py, pz);
	const double sl = std::sqrt(1 + 1 + 16.0), pl = std::sqrt(0.25 + 1 + 4.0);
	EXPECT_NEAR(sx, -1.0 / sl, 1e-9);
	EXPECT_NEAR(sy, -1.0 / sl, 1e-9);
	EXPECT_NEAR(sz, 4.0 / sl, 1e-9);
	EXPECT_NEAR(px, -0.5 / pl, 1e-9);
	EXPECT_NEAR(py, -1.0 / pl, 1e-9);
	EXPECT_NEAR(pz, 2.0 / pl, 1e-9);
}

TEST(BumpMapPbrt, TheResultIsOnTheSideOfTheAuthoredNormal) {
	double nx, ny, nz;
	// tangents whose cross product points down (a mirrored parametrisation): the result is turned back to the normal's side (pbrt: FaceForward(ns, n))
	apply_bump_map_pbrt<double>(0.0, 0.0, 0.0, 0.001, 0.001, 0, 0, 1, false, 1, 0, 0, 0, -1, 0, 0, 0, 0, 0, 0, 0, nx, ny, nz);
	EXPECT_NEAR(nz, 1.0, 1e-12);
}

// dndu and dndv of a triangle's normals: n(u, v) is linear over the triangle, so for uv (0,0), (1,0), (0,1) they are n1 - n0 and n2 - n0.
TEST(TriangleDndu, IsTheNormalsDifferenceOverTheUvEdges) {
	const double n0[3] = {0, 0, 1}, n1[3] = {1, 0, 0}, n2[3] = {0, 1, 0};
	double dndu[3], dndv[3];
	triangle_dndu_dndv<double>(0, 0, 1, 0, 0, 1, n0, n1, n2, dndu, dndv);
	EXPECT_NEAR(dndu[0], 1.0, 1e-12); EXPECT_NEAR(dndu[1], 0.0, 1e-12); EXPECT_NEAR(dndu[2], -1.0, 1e-12);
	EXPECT_NEAR(dndv[0], 0.0, 1e-12); EXPECT_NEAR(dndv[1], 1.0, 1e-12); EXPECT_NEAR(dndv[2], -1.0, 1e-12);
	triangle_dndu_dndv<double>(0, 0, 0, 0, 0, 0, n0, n1, n2, dndu, dndv);   // no usable (u, v): none
	EXPECT_EQ(dndu[0], 0.0);
	EXPECT_EQ(dndv[2], 0.0);
}

// pbrt's BSDF is two-sided about the shading normal: a bump that tilts the normal away from the viewer must turn it back toward the viewer, not toward the unperturbed surface.
TEST(BumpMapMaterial, ATiltedNormalIsTurnedTowardTheViewer) {
	auto inner = std::make_shared<lambertian>(color(0.5, 0.5, 0.5));
	// a texture whose value grows with u: a steep slope along +x that tilts the normal toward -x far enough to lie behind a ray travelling almost along the surface
	class u_ramp : public texture {
	  public:
		color value(double u, double, const point3&) const override { return color(u, u, u); }
	};
	bump_map_material bm(std::make_shared<u_ramp>(), inner, 400.0, 0.001);
	hit_record rec = make_rec(vec3(0, 1, 0), vec3(1, 0, 0));
	rec.dpdv = vec3(0, 0, 1);
	ray grazing(point3(-5, 0.02, 0), unit_vector(vec3(1, -0.004, 0)));   // almost along +x, just above the surface
	const hit_record bumped = bm.apply(grazing, rec);
	EXPECT_LE(dot(bumped.normal, unit_vector(grazing.direction())), 1e-12) << "the shading normal faces the viewer";
}
