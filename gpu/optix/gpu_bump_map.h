#pragma once
// gpu_bump_map.h -- pbrt-v4 BumpMap on the GPU (both backends' triangle closest-hit programs).
//
// The CPU wraps a material in bump_map_material (normal_map_materials.h) when its pbrt "texture displacement"
// resolves to a grayscale image: displacement = scale * red(texture(u, v)) read with a bilinear, Repeat lookup,
// finite differences at +step along u and v (0.001 where there is no ray footprint, i.e. every bounce hit), then
// the shared apply_bump_map(). This reproduces that for any triangle hit, so the GPU stops rendering a bumped
// surface flat (Sibenik Cathedral was 12-15% darker than the CPU for this reason alone).
//
// The finite-difference step is the CPU's too: 0.001 for a bounce hit, and for a primary camera-ray hit
// 0.5 * (|dudx| + |dudy|) from the pixel's footprint on the surface (gpu_bump_footprint_step). That footprint
// matters: on a distant or heavily tiled surface (Sibenik's stone) it spans many texels, so the CPU's primary
// bump is a smoothed one, while a fixed 0.001 step reads the texture at full detail and renders it ~25% too
// bright.

#include "optix_types.h"
#include "optix_math_helpers.h"
#include "../../src/shared/normal_map.h"
#include "../../src/shared/camera_footprint.h"   // pbrt's Approximate_dp_dxy for a hit whose ray has no differentials

// What the pixel's footprint on the surface says in the surface's (u, v): the finite-difference step 0.5 * (|dudx| + |dudy|) and the four derivatives that choose a mip level.
// All zero where there is no footprint (a camera that gives none, or a degenerate mapping).
struct GpuBumpDiff {
	float step;
	float dudx, dvdx, dudy, dvdy;
};

// Bilinear, wrap-aware read of the red channel, the convention of sample_texture() and of the CPU's
// bilinear_wrap_texture (texel centres at (i + 0.5) / size, image row 0 at the top).
inline __host__ __device__ float gpu_sample_displacement(const TextureData* textures, const unsigned char* pixels,
														 int texIdx, float u, float v) {
	const TextureData& t = textures[texIdx];
	if (t.width <= 0 || t.height <= 0) return 0.0f;
	const float um = t.uvScaleU * u + t.uvDeltaU;   // the displacement Texture's own UVMapping: the finite differences below are then across the surface's own coordinates, as the CPU's
	const float vm = t.uvScaleV * v + t.uvDeltaV;
	const float uw = fminf(fmaxf(um, -1024.0f), 1024.0f);
	const float vw = fminf(fmaxf(1.0f - vm, -1024.0f), 1024.0f);
	const double x = (double)uw * t.width - 0.5;
	const double y = (double)vw * t.height - 0.5;
	const int x0 = static_cast<int>(floor(x)), y0 = static_cast<int>(floor(y));
	const float fx = (float)(x - x0), fy = (float)(y - y0);
	auto texel = [&](int xi, int yi) -> float {
		switch (t.wrapMode) {
			case GpuWrapMode::Repeat:
				xi = ((xi % t.width) + t.width) % t.width;
				yi = ((yi % t.height) + t.height) % t.height;
				break;
			case GpuWrapMode::Black:
				if (xi < 0 || xi >= t.width || yi < 0 || yi >= t.height) return 0.0f;
				break;
			case GpuWrapMode::Clamp:
				xi = xi < 0 ? 0 : (xi >= t.width ? t.width - 1 : xi);
				yi = yi < 0 ? 0 : (yi >= t.height ? t.height - 1 : yi);
				break;
		}
		if (t.heightRange > 0.0f) return t.heightLo + t.heightRange * (pixels[t.pixelOffset + (yi * t.width + xi) * 3] * (1.0f / 255.0f));   // (a mip level: see TextureData::heightLo)
		return texel_rgb(&pixels[t.pixelOffset + (yi * t.width + xi) * 3], t.srgb).x;
	};
	return (1.0f - fy) * ((1.0f - fx) * texel(x0, y0) + fx * texel(x0 + 1, y0)) +
				  fy  * ((1.0f - fx) * texel(x0, y0 + 1) + fx * texel(x0 + 1, y0 + 1));
}

// The height at (u, v) from the mip level that fits `footprint` = pbrt's 2 * max(|dsdx|, |dtdx|, |dsdy|, |dtdy|) in the texture's own (s, t) coordinates (MIPMap::Filter with the
// default Bilinear filter: level = levels - 1 + log2(width); the coarsest texel when that reaches the last level; otherwise the bilinear lookup in floor(level)). A footprint
// of 0, or a height map with no chain, is the full-resolution lookup. This is what makes a finely textured floor seen from afar flat in pbrt, not noisy.
inline __host__ __device__ float gpu_sample_displacement_filtered(const TextureData* textures, const unsigned char* pixels,
																  int texIdx, float u, float v, float footprint) {
	const TextureData& t0 = textures[texIdx];
	if (t0.bumpLevels <= 0 || t0.bumpFirstLevel < 0) return gpu_sample_displacement(textures, pixels, texIdx, u, v);
	const int level0 = t0.bumpLevel0 >= 0 ? t0.bumpLevel0 : texIdx;   // (level 0 of the pyramid: the resampled picture for sides that are not powers of two)
	if (!(footprint > 0.0f)) return gpu_sample_displacement(textures, pixels, level0, u, v);
	const int levels = t0.bumpLevels + 1;
	const float level = (float)(levels - 1) + log2f(fmaxf(footprint, 1e-8f));
	if (level >= (float)(levels - 1)) return gpu_sample_displacement(textures, pixels, t0.bumpFirstLevel + t0.bumpLevels - 1, 0.5f, 0.5f);   // (the 1x1 level)
	const int i = level > 0.0f ? (int)floorf(level) : 0;
	return gpu_sample_displacement(textures, pixels, i == 0 ? level0 : t0.bumpFirstLevel + i - 1, u, v);
}

// `n` is the unit shading normal AS AUTHORED (not turned toward the ray: pbrt's BumpMap pushes along shading.n), `dpdu` and `dpdv` the UNNORMALIZED surface tangents (world units per unit
// u and v, as the CPU's rec.dpdu and rec.dpdv): the finite-difference slope is per unit u, so their length sets how steep the relief reads. `smooth`: the mesh has vertex normals, whose
// shading tangents pbrt re-orthogonalises; dndu / dndv are the derivatives of the interpolated normal (the displacement * dndu term). Returns the bumped normal on n's side; the caller
// turns it toward the ray as it did n. The same apply_bump_map_pbrt() as the CPU's bump_map_material.
inline __host__ __device__ float3 gpu_bump_shading_normal(const TextureData* textures, const unsigned char* pixels,
														  int texIdx, float scale, float u, float v,
														  const float3& n, bool smooth, const float3& dpdu, const float3& dpdv_in,
														  const float3& dndu, const float3& dndv, const GpuBumpDiff& footprint) {
	const float step = footprint.step > 1e-8f ? footprint.step : 0.001f;   // 0.001: the CPU's no-footprint fallback
	// The height is read through the footprint of the pixel in the displacement texture's own coordinates, like the CPU (pbrt hands the shifted context's dudx.. on).
	float width = 0.0f;
	if (footprint.step > 1e-8f) {
		const TextureData& t0 = textures[texIdx];
		width = 2.0f * fmaxf(fmaxf(fabsf(t0.uvScaleU * footprint.dudx), fabsf(t0.uvScaleV * footprint.dvdx)),
							 fmaxf(fabsf(t0.uvScaleU * footprint.dudy), fabsf(t0.uvScaleV * footprint.dvdy)));
	}
	const float disp   = scale * gpu_sample_displacement_filtered(textures, pixels, texIdx, u,        v,        width);
	const float disp_u = scale * gpu_sample_displacement_filtered(textures, pixels, texIdx, u + step, v,        width);
	const float disp_v = scale * gpu_sample_displacement_filtered(textures, pixels, texIdx, u,        v + step, width);
	float3 dpdv = dpdv_in;
	if (dot(dpdv, dpdv) < 1e-12f) dpdv = cross(n, dpdu);
	if (dot(dpdv, dpdv) < 1e-12f) dpdv = make_float3(0.0f, 1.0f, 0.0f);
	float ox, oy, oz;
	apply_bump_map_pbrt<float>(disp, disp_u, disp_v, step, step, n.x, n.y, n.z, smooth, dpdu.x, dpdu.y, dpdu.z,
							   dpdv.x, dpdv.y, dpdv.z, dndu.x, dndu.y, dndu.z, dndv.x, dndv.y, dndv.z, ox, oy, oz);
	return make_float3(ox, oy, oz);
}

// dndu / dndv of a triangle's interpolated normal (the CPU's triangle_dndu_dndv, with pbrt's default uv for a mesh that has none).
inline __host__ __device__ void gpu_triangle_dndu_dndv(const float3& n0, const float3& n1, const float3& n2,
													   const float2& uv0, const float2& uv1, const float2& uv2, bool hasUVs,
													   float3& dndu, float3& dndv) {
	const float a0[3] = {n0.x, n0.y, n0.z}, a1[3] = {n1.x, n1.y, n1.z}, a2[3] = {n2.x, n2.y, n2.z};
	float du[3], dv[3];
	if (hasUVs) triangle_dndu_dndv<float>(uv0.x, uv0.y, uv1.x, uv1.y, uv2.x, uv2.y, a0, a1, a2, du, dv);
	else        triangle_dndu_dndv<float>(0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, a0, a1, a2, du, dv);
	dndu = make_float3(du[0], du[1], du[2]);
	dndv = make_float3(dv[0], dv[1], dv[2]);
}

// The CPU's rec.dpdu and rec.dpdv for a triangle: the 2x2 UV-edge Jacobian solve, UNNORMALIZED, in double. The
// solve cancels badly in float on a small triangle with tiny UV deltas, so it is done in double as on the CPU.
// A degenerate UV mapping falls back to a unit edge for dpdu and cross(n, dpdu) for dpdv, as the CPU does.
inline __host__ __device__ void gpu_triangle_dpdu_dpdv(const float3& p0, const float3& p1, const float3& p2,
													   const float2& uv0, const float2& uv1, const float2& uv2,
													   const float3& n, float3& dpdu, float3& dpdv) {
	const double e1x = (double)p1.x - p0.x, e1y = (double)p1.y - p0.y, e1z = (double)p1.z - p0.z;
	const double e2x = (double)p2.x - p0.x, e2y = (double)p2.y - p0.y, e2z = (double)p2.z - p0.z;
	const double du1 = (double)uv1.x - uv0.x, dv1 = (double)uv1.y - uv0.y;
	const double du2 = (double)uv2.x - uv0.x, dv2 = (double)uv2.y - uv0.y;
	const double det = du1 * dv2 - dv1 * du2;
	if (fabs(det) > 1e-12) {
		const double inv = 1.0 / det;
		dpdu = make_float3((float)(inv * (dv2 * e1x - dv1 * e2x)),
						   (float)(inv * (dv2 * e1y - dv1 * e2y)),
						   (float)(inv * (dv2 * e1z - dv1 * e2z)));
		dpdv = make_float3((float)(inv * (-du2 * e1x + du1 * e2x)),
						   (float)(inv * (-du2 * e1y + du1 * e2y)),
						   (float)(inv * (-du2 * e1z + du1 * e2z)));
		return;
	}
	const double len2 = e1x * e1x + e1y * e1y + e1z * e1z;
	if (len2 > 1e-14) {
		const double inv = 1.0 / sqrt(len2);
		dpdu = make_float3((float)(e1x * inv), (float)(e1y * inv), (float)(e1z * inv));
	} else {
		dpdu = make_float3(1.0f, 0.0f, 0.0f);
	}
	dpdv = cross(n, dpdu);
}

// The viewport of a perspective pinhole/thin-lens camera, enough to rebuild the two one-pixel-offset rays the
// CPU's get_ray() attaches to a primary ray. `enabled` is false for every other camera (orthographic,
// spherical, realistic, animated), which the CPU gives no differentials either.
struct GpuBumpFootprint {
	float3 origin;       // the camera's position: pbrt gives a hit its footprint as if a camera ray at this origin saw the point (CameraBase::Approximate_dp_dxy)
	int    enabled;
	float3 lowerLeft;    // the viewport's lower left corner relative to `origin` (GpuCameraParams::lower_left_offset)
	float3 horizontal;   // the whole viewport's extent, so one pixel is horizontal / width
	float3 vertical;
	camera_footprint::Camera approx;   // the camera's minimum differentials, made once on the host (gpu_make_bump_footprint)
	int    width, height;
	float  diffScale;    // pbrt scales camera ray differentials by max(1/8, 1/sqrt(samples per pixel)): a bump map's step is that much finer at many samples than at one
};

inline __host__ __device__ GpuBumpFootprint gpu_make_bump_footprint(const GpuCameraParams& cam, int width, int height, int samplesPerPixel = 1) {
	GpuBumpFootprint f;
	f.origin     = cam.origin;
	f.enabled    = (cam.kind == CameraKind::Perspective && !cam.animated && width > 0 && height > 0) ? 1 : 0;
	f.lowerLeft  = gpu_camera_lower_left_offset(cam);
	f.horizontal = cam.horizontal;
	f.vertical   = cam.vertical;
	f.width      = width;
	f.height     = height;
	f.diffScale  = fmaxf(0.125f, 1.0f / sqrtf((float)(samplesPerPixel > 1 ? samplesPerPixel : 1)));
	f.approx     = camera_footprint::Camera();
#ifndef __CUDA_ARCH__
	// (host only: it searches 512 film points; the launch parameters carry the result to the device)
	if (f.enabled) {
		const auto v = [](const float3& a) { return camera_footprint::make(a.x, a.y, a.z); };
		const camera_footprint::V3 du = v(cam.horizontal) * (1.0 / width), dv = v(cam.vertical) * (-1.0 / height);
		const camera_footprint::V3 pixel00 = v(cam.origin) + v(f.lowerLeft) + v(cam.horizontal) * (0.5 / width) + v(cam.vertical) * (1.0 - 0.5 / height);
		f.approx = camera_footprint::makeCamera(v(cam.origin), pixel00, du, dv, width, height, samplesPerPixel);
	}
#endif
	return f;
}

// The two least-squares unknowns of SurfaceInteraction::compute_differentials(): (dudx, dvdx) and (dudy, dvdy) from the surface offsets dpdx and dpdy of one pixel step and
// the tangents dpdu and dpdv. Zero where the mapping is degenerate.
inline __host__ __device__ GpuBumpDiff gpu_bump_solve(const float3& dpdx, const float3& dpdy, const float3& dpdu, const float3& dpdv) {
	const GpuBumpDiff none = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
	const double a00 = dot(dpdu, dpdu), a01 = dot(dpdu, dpdv), a11 = dot(dpdv, dpdv);
	const double det = a00 * a11 - a01 * a01;
	if (!(fabs(det) > 0.0)) return none;
	const double inv = 1.0 / det;
	const double bx0 = dot(dpdu, dpdx), bx1 = dot(dpdv, dpdx);
	const double by0 = dot(dpdu, dpdy), by1 = dot(dpdv, dpdy);
	const double dudx = (a11 * bx0 - a01 * bx1) * inv;
	const double dudy = (a11 * by0 - a01 * by1) * inv;
	const double dvdx = (a00 * bx1 - a01 * bx0) * inv;
	const double dvdy = (a00 * by1 - a01 * by0) * inv;
	const double step = 0.5 * (fabs(dudx) + fabs(dudy));
	if (!(step == step && step < 1e8)) return none;
	GpuBumpDiff out;
	out.step = (float)step;
	out.dudx = (float)dudx; out.dvdx = (float)dvdx; out.dudy = (float)dudy; out.dvdy = (float)dvdy;
	return out;
}

// 0.5 * (|dudx| + |dudy|) for a primary ray (origin O, unit direction d) that hit the surface at p: the two
// neighbouring-pixel rays, offset by one pixel on the viewport and sharing O, are intersected with the tangent
// plane (n, p) and the offsets projected onto (dpdu, dpdv) by least squares - the CPU's
// SurfaceInteraction::compute_differentials(). Returns 0 where the CPU would have no footprint (not a
// perspective camera, a ray parallel to the plane, a degenerate mapping), which callers read as "use 0.001".
inline __host__ __device__ GpuBumpDiff gpu_bump_footprint(const GpuBumpFootprint& f, const float3& O, const float3& d,
														  const float3& p, const float3& n,
														  const float3& dpdu, const float3& dpdv) {
	const GpuBumpDiff none = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
	if (!f.enabled) return none;
	float3 pn = cross(f.horizontal, f.vertical);
	const float pnLen = sqrtf(dot(pn, pn));
	if (pnLen < 1e-12f) return none;
	pn = pn * (1.0f / pnLen);
	const float den = dot(pn, d);
	if (fabsf(den) < 1e-8f) return none;
	// Length of the UNnormalized primary direction (pixel sample - ray origin): it lands on the viewport plane.
	const float L = dot(pn, f.lowerLeft - (O - f.origin)) / den;
	const float3 D  = d * L;
	const float3 Dx = D + f.horizontal * (f.diffScale / (float)f.width);
	const float3 Dy = D + f.vertical   * (f.diffScale / (float)f.height);
	const float dnx = dot(n, Dx), dny = dot(n, Dy);
	if (dnx == 0.0f || dny == 0.0f) return none;
	const float3 pop = p - O;
	const float3 dpdx = Dx * (dot(n, pop) / dnx) - pop;     // (O + tx * Dx) - p
	const float3 dpdy = Dy * (dot(n, pop) / dny) - pop;
	return gpu_bump_solve(dpdx, dpdy, dpdu, dpdv);
}

// The footprint of a hit at p as pbrt-v4 gives one to every hit whose ray has no differentials (every diffuse or glossy bounce): CameraBase::Approximate_dp_dxy, the camera's
// smallest pixel differentials seen in the direction of p (src/shared/camera_footprint.h), not a camera ray through p. A hit that is the camera ray's own uses gpu_bump_footprint().
inline __host__ __device__ GpuBumpDiff gpu_bump_footprint_at(const GpuBumpFootprint& f, const float3& p, const float3& n,
															 const float3& dpdu, const float3& dpdv) {
	if (!f.enabled) return GpuBumpDiff{0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
	camera_footprint::V3 dpdx, dpdy;
	if (!camera_footprint::approximate(f.approx, camera_footprint::make(p.x, p.y, p.z), camera_footprint::make(n.x, n.y, n.z), dpdx, dpdy)) return GpuBumpDiff{0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
	return gpu_bump_solve(make_float3((float)dpdx.x, (float)dpdx.y, (float)dpdx.z), make_float3((float)dpdy.x, (float)dpdy.y, (float)dpdy.z), dpdu, dpdv);
}

