#pragma once
//==============================================================================
// normal_map.h -- Normal mapping and bump mapping math (pbrt-v4 aligned)
//
// pbrt-v4 alignment
// -----------------
// pbrt-v4 implements both techniques in materials.h:
//
//   NormalMap()  -- samples an RGB tangent-space normal map texture, decodes
//                   the normal vector, transforms it to world space using the
//                   surface shading frame (dpdu, n), then recomputes dpdu/dpdv
//                   via Gram-Schmidt so the new shading normal is consistent.
//                   Reference: pbrt-v4 materials.h:86-105
//
//   BumpMap()    -- evaluates a scalar displacement texture at the hit point
//                   and at two finite-difference offsets along dpdu and dpdv,
//                   then perturbs dpdu/dpdv using the displacement gradient.
//                   The new shading normal is cross(dpdu_new, dpdv_new).
//                   Reference: pbrt-v4 materials.h:107-145
//
// This file provides CPU/GPU-portable implementations that operate on plain
// scalar fields (no pbrt Image/TextureEvaluator dependencies).  The caller
// is responsible for sampling the texture.
//
// Inputs (shared by both functions):
//   nx,ny,nz         -- geometric surface normal (world space, unit)
//   dpdu_x/y/z       -- surface tangent along U (world space, not necessarily unit)
//                       pbrt-v4: ctx.shading.dpdu
//   dpdv_x/y/z       -- surface tangent along V (world space)
//                       pbrt-v4: ctx.shading.dpdv
//
// For NormalMap, the caller also provides the decoded tangent-space normal.
// For BumpMap, the caller provides three displacement values.
//
// Output:
//   perturbed_nx/ny/nz -- new shading normal (world space, unit)
//==============================================================================

#include "cpu_gpu.h"

#if defined(__CUDACC__)
#   include <cuda_runtime.h>
#else
#   include <cmath>
#endif

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

template<typename T>
CPU_GPU T nm_dot(T ax, T ay, T az, T bx, T by, T bz) {
	return ax*bx + ay*by + az*bz;
}

template<typename T>
CPU_GPU void nm_cross(T ax, T ay, T az, T bx, T by, T bz,
					  T& cx, T& cy, T& cz) {
	cx = ay*bz - az*by;
	cy = az*bx - ax*bz;
	cz = ax*by - ay*bx;
}

template<typename T>
CPU_GPU void nm_normalize(T& x, T& y, T& z) {
#if defined(__CUDACC__)
	T len = sqrtf(x*x + y*y + z*z);
#else
	T len = std::sqrt(x*x + y*y + z*z);
#endif
	if (len > T(1e-8)) { x /= len; y /= len; z /= len; }
}

// Gram-Schmidt: project v onto the plane perpendicular to n.
// Returns v - dot(v,n)*n, does not normalise.
template<typename T>
CPU_GPU void nm_gram_schmidt(T vx, T vy, T vz,
							 T nx, T ny, T nz,
							 T& ox, T& oy, T& oz) {
	T d = nm_dot(vx, vy, vz, nx, ny, nz);
	ox = vx - d*nx;
	oy = vy - d*ny;
	oz = vz - d*nz;
}

// ---------------------------------------------------------------------------
// apply_normal_map
//
// pbrt-v4 equivalent: NormalMap() in materials.h
//
// Decode a tangent-space normal (ns_t) and transform it to world space using
// the surface shading frame (dpdu, n), then return the perturbed shading normal.
//
// Parameters:
//   ns_tx/y/z  -- tangent-space normal decoded from the map, range [-1,1]^3
//                 (caller: decode as 2*RGB - 1, then normalise)
//   nx/ny/nz   -- geometric shading normal (world space, unit)
//   dpdu_x/y/z -- surface tangent along U (world space, may be non-unit)
//
// Output:
//   out_nx/ny/nz -- perturbed shading normal (world space, unit)
//
// Steps (identical to pbrt-v4):
//   1. Build Frame from (normalize(dpdu), n)  -- tangent + normal define X/Z axes
//   2. Transform ns_t from local frame to world frame
//   3. Normalise the result
// ---------------------------------------------------------------------------
template<typename T>
CPU_GPU void apply_normal_map(
	T ns_tx, T ns_ty, T ns_tz,   // decoded tangent-space normal
	T nx,    T ny,    T nz,      // geometric shading normal (unit)
	T dpdu_x, T dpdu_y, T dpdu_z, // surface tangent along U
	T& out_nx, T& out_ny, T& out_nz)
{
	// Normalise dpdu to get the tangent axis (X axis of the shading frame)
	T tx = dpdu_x, ty = dpdu_y, tz = dpdu_z;
	nm_normalize(tx, ty, tz);

	// Gram-Schmidt: make tangent perpendicular to normal (numerical safety)
	T gx, gy, gz;
	nm_gram_schmidt(tx, ty, tz, nx, ny, nz, gx, gy, gz);
	nm_normalize(gx, gy, gz);

	// Bitangent = cross(n, tangent)  -- Z x X = Y in right-handed frame
	T bx, by, bz;
	nm_cross(nx, ny, nz, gx, gy, gz, bx, by, bz);
	nm_normalize(bx, by, bz);

	// Transform ns_t from tangent space to world space:
	//   world_normal = ns_t.x * tangent + ns_t.y * bitangent + ns_t.z * normal
	// (pbrt-v4: frame.FromLocal(ns))
	out_nx = ns_tx * gx + ns_ty * bx + ns_tz * nx;
	out_ny = ns_tx * gy + ns_ty * by + ns_tz * ny;
	out_nz = ns_tx * gz + ns_ty * bz + ns_tz * nz;
	nm_normalize(out_nx, out_ny, out_nz);

	// Ensure the perturbed normal is on the same side as the geometric normal
	// (avoids flipping when the map contains extreme values)
	if (nm_dot(out_nx, out_ny, out_nz, nx, ny, nz) < T(0)) {
		out_nx = -out_nx; out_ny = -out_ny; out_nz = -out_nz;
	}
}

// ---------------------------------------------------------------------------
// apply_bump_map
//
// pbrt-v4 equivalent: BumpMap() in materials.h
//
// Perturb the shading normal using finite differences of a displacement field.
//
// Parameters:
//   disp       -- displacement at the hit point
//   disp_u     -- displacement at hit point + du along dpdu
//   disp_v     -- displacement at hit point + dv along dpdv
//   du, dv     -- finite-difference step sizes (pbrt uses 0.5*(|dudx|+|dudy|))
//   nx/ny/nz   -- geometric shading normal (unit)
//   dpdu_x/y/z -- surface tangent along U (world space)
//   dpdv_x/y/z -- surface tangent along V (world space)
//
// Output:
//   out_nx/ny/nz -- perturbed shading normal (world space, unit)
//
// Math (identical to pbrt-v4):
//   dpdu_new = dpdu + (disp_u - disp) / du * n + disp * dndu  (dndu≈0 here)
//   dpdv_new = dpdv + (disp_v - disp) / dv * n + disp * dndv  (dndv≈0 here)
//   n_new    = normalize(cross(dpdu_new, dpdv_new))
//
// Note: we omit the dndu/dndv terms (normal curvature derivatives) as they
// are not tracked in hit_record.  This matches the simplified version used
// in many renderers and is accurate for flat/low-curvature surfaces.
// ---------------------------------------------------------------------------
template<typename T>
CPU_GPU void apply_bump_map(
	T disp, T disp_u, T disp_v,  // displacement samples
	T du,    T dv,                // finite-difference steps
	T nx,    T ny,    T nz,       // geometric shading normal (unit)
	T dpdu_x, T dpdu_y, T dpdu_z, // surface tangent along U
	T dpdv_x, T dpdv_y, T dpdv_z, // surface tangent along V
	T& out_nx, T& out_ny, T& out_nz)
{
	// Perturb dpdu and dpdv by the displacement gradient
	// pbrt-v4: *dpdu = dpdu + (uDisplace-disp)/du * n  (+  disp * dndu term omitted)
	T grad_u = (disp_u - disp) / du;
	T grad_v = (disp_v - disp) / dv;

	T new_dpdu_x = dpdu_x + grad_u * nx;
	T new_dpdu_y = dpdu_y + grad_u * ny;
	T new_dpdu_z = dpdu_z + grad_u * nz;

	T new_dpdv_x = dpdv_x + grad_v * nx;
	T new_dpdv_y = dpdv_y + grad_v * ny;
	T new_dpdv_z = dpdv_z + grad_v * nz;

	// New normal = cross(dpdu_new, dpdv_new)
	nm_cross(new_dpdu_x, new_dpdu_y, new_dpdu_z,
			 new_dpdv_x, new_dpdv_y, new_dpdv_z,
			 out_nx, out_ny, out_nz);
	nm_normalize(out_nx, out_ny, out_nz);

	// Fallback: if cross product degenerates, keep geometric normal
#if defined(__CUDACC__)
	T len2 = out_nx*out_nx + out_ny*out_ny + out_nz*out_nz;
	if (len2 < T(1e-8)) { out_nx = nx; out_ny = ny; out_nz = nz; }
#else
	if (!std::isfinite(out_nx) || !std::isfinite(out_ny) || !std::isfinite(out_nz)
		|| out_nx*out_nx + out_ny*out_ny + out_nz*out_nz < T(1e-8)) {
		out_nx = nx; out_ny = ny; out_nz = nz;
	}
#endif

	// Ensure the bumped normal faces the same side as the geometric normal
	if (nm_dot(out_nx, out_ny, out_nz, nx, ny, nz) < T(0)) {
		out_nx = -out_nx; out_ny = -out_ny; out_nz = -out_nz;
	}
}

// ---------------------------------------------------------------------------
// triangle_dndu_dndv
//
// pbrt-v4 Triangle::InteractionFromIntersection: the derivatives of the interpolated shading normal with respect to u and v,
// from the vertex normals and the vertices' texture coordinates (pbrt's dndu / dndv, which a bump map's `displacement * dndu`
// term needs). n0..n2 are the three vertex normals, each as (x, y, z). A degenerate parametrisation gives zero (pbrt then picks
// an arbitrary frame: a surface with no usable (u, v) has no bump either).
// ---------------------------------------------------------------------------
template<typename T>
CPU_GPU void triangle_dndu_dndv(T u0, T v0, T u1, T v1, T u2, T v2,
								const T* n0, const T* n1, const T* n2,
								T* dndu, T* dndv)
{
	const T du02 = u0 - u2, dv02 = v0 - v2, du12 = u1 - u2, dv12 = v1 - v2;
	const T det = du02 * dv12 - dv02 * du12;
#if defined(__CUDACC__)
	const bool degenerate = fabsf(det) < T(1e-9);
#else
	const bool degenerate = std::fabs(det) < T(1e-9);
#endif
	if (degenerate) {
		for (int a = 0; a < 3; ++a) { dndu[a] = T(0); dndv[a] = T(0); }
		return;
	}
	const T inv = T(1) / det;
	for (int a = 0; a < 3; ++a) {
		const T dn1 = n0[a] - n2[a], dn2 = n1[a] - n2[a];
		dndu[a] = (dv12 * dn1 - dv02 * dn2) * inv;
		dndv[a] = (du02 * dn2 - du12 * dn1) * inv;
	}
}

// ---------------------------------------------------------------------------
// apply_bump_map_pbrt
//
// pbrt-v4 BumpMap() (materials.h) with the shading geometry pbrt gives it:
//   dpdu' = shading.dpdu + (disp_u - disp) / du * n + disp * dndu
//   dpdv' = shading.dpdv + (disp_v - disp) / dv * n + disp * dndv
//   ns    = normalize(cross(dpdu', dpdv')), on the side of n
// where n is the shading normal AS AUTHORED (not turned toward the ray), and shading.dpdu / dpdv are, for a surface that has shading
// normals (`smooth`: a triangle mesh with vertex normals), pbrt's Triangle tangents: ts = cross(ns, dpdu), ss = cross(ts, ns), so
// both are perpendicular to ns and the same length; for any other surface they are its own dpdu and dpdv. The `disp * dndu` term is
// what keeps a tall relief on a curved surface (a ball bumped by 0.3 of its radius) the shape pbrt draws: omitting it made a
// bump of height 1 on a dome 2-5% darker than pbrt's picture.
// ---------------------------------------------------------------------------
template<typename T>
CPU_GPU void apply_bump_map_pbrt(
	T disp, T disp_u, T disp_v,
	T du,    T dv,
	T nx,    T ny,    T nz,          // shading normal as authored (unit)
	bool smooth,
	T dpdu_x, T dpdu_y, T dpdu_z,
	T dpdv_x, T dpdv_y, T dpdv_z,
	T dndu_x, T dndu_y, T dndu_z,
	T dndv_x, T dndv_y, T dndv_z,
	T& out_nx, T& out_ny, T& out_nz)
{
	T su_x = dpdu_x, su_y = dpdu_y, su_z = dpdu_z;
	T sv_x = dpdv_x, sv_y = dpdv_y, sv_z = dpdv_z;
	if (smooth) {
		T tx, ty, tz;
		nm_cross(nx, ny, nz, dpdu_x, dpdu_y, dpdu_z, tx, ty, tz);   // ts = cross(ns, ss)
		if (tx*tx + ty*ty + tz*tz > T(0)) {
			nm_cross(tx, ty, tz, nx, ny, nz, su_x, su_y, su_z);       // ss = cross(ts, ns)
			sv_x = tx; sv_y = ty; sv_z = tz;
		} else {
			// dpdu along the normal: any frame round it (pbrt's CoordinateSystem)
#if defined(__CUDACC__)
			const T sign = copysignf(T(1), nz);
#else
			const T sign = std::copysign(T(1), nz);
#endif
			const T a = T(-1) / (sign + nz), b = nx * ny * a;
			su_x = T(1) + sign * nx * nx * a; su_y = sign * b; su_z = -sign * nx;
			sv_x = b; sv_y = sign + ny * ny * a; sv_z = -ny;
		}
	}
	const T gu = (disp_u - disp) / du, gv = (disp_v - disp) / dv;
	const T bu_x = su_x + gu * nx + disp * dndu_x, bu_y = su_y + gu * ny + disp * dndu_y, bu_z = su_z + gu * nz + disp * dndu_z;
	const T bv_x = sv_x + gv * nx + disp * dndv_x, bv_y = sv_y + gv * ny + disp * dndv_y, bv_z = sv_z + gv * nz + disp * dndv_z;
	nm_cross(bu_x, bu_y, bu_z, bv_x, bv_y, bv_z, out_nx, out_ny, out_nz);
	const T len2 = out_nx*out_nx + out_ny*out_ny + out_nz*out_nz;
#if defined(__CUDACC__)
	const bool bad = !(len2 > T(1e-16)) || !isfinite(len2);
#else
	const bool bad = !(len2 > T(1e-16)) || !std::isfinite(len2);
#endif
	if (bad) { out_nx = nx; out_ny = ny; out_nz = nz; return; }
	nm_normalize(out_nx, out_ny, out_nz);
	if (nm_dot(out_nx, out_ny, out_nz, nx, ny, nz) < T(0)) { out_nx = -out_nx; out_ny = -out_ny; out_nz = -out_nz; }
}
