#pragma once
// optix_device_media_helpers.h -- the heterogeneous-medium and box helpers (cloud density, RGB-grid lookup, box slab test and face normal).
// Part of optix_device_helpers.h (included by it, last). They used to sit at the top of optix_intersection_sphere.h, but the shadow any-hit and probe programs
// use them too, and once the recursive programs became several modules (optix_programs_*.cu) every module needs them.


// GPU-only cloud density: the 5-octave FBm + altitude-falloff portion of
// CloudMedium<T>::compute_density (src/shared/cloud_medium.h), WITHOUT the
// optional wispiness/dnoise gradient perturbation CPU renders with. Not a
// call to compute_density() itself - calling that member function directly
// from this backend's one-thread-per-pixel mega-kernel closesthit program
// reproducibly stalled mid-computation (confirmed via device printf: entry
// into the delta-tracking loop and its dt/tt values print fine, but nothing
// after the compute_density() call ever does, even though the loop's own
// t-bounds are otherwise correct) - some interaction between this backend's
// call graph and that member function specifically that wasn't pinned down
// despite substantial isolation (ruled out: sigma_s/iteration-cap values,
// #pragma unroll, marking dnoise or compute_density __noinline__ - the
// latter "fixed" the timing but corrupted dnoise's reference-parameter
// outputs, visually a flat-sided box instead of a wispy cloud). This hand-
// duplicated version - proven correct and fast via the same isolation - is
// used on both GPU backends instead (see wavefront_kernels.cu's copy).
// Deliberate, documented CPU/GPU divergence: GPU renders a smoother cloud
// silhouette missing CPU's fine wispy edge detail, in the same spirit as
// this codebase's existing "no NEE/MIS for GPU volume scattering" gap.
__device__ __forceinline__ float gpu_cloud_density(const CloudMedium<float>& cloud,
													 float mx, float my, float mz) {
	float ppx = cloud.frequency * mx;
	float ppy = cloud.frequency * my;
	float ppz = cloud.frequency * mz;
	float d = 0.0f;
	float omega = 0.5f, lambda = 1.0f;
	for (int oct = 0; oct < 5; ++oct) {
		d += omega * perlin_noise<float>(lambda * ppx, lambda * ppy, lambda * ppz);
		omega *= 0.5f;
		lambda *= 1.99f;
	}
	d = fminf(1.0f, fmaxf(0.0f, (1.0f - my) * 4.5f * cloud.density * d));
	float extra = 2.0f * fmaxf(0.0f, 0.5f - my);
	return fminf(1.0f, fmaxf(0.0f, d + extra));
}

// Voxel fetch (zero outside the grid) for gpu_rgb_grid_trilinear below - `d` is one channel
// block (nx*ny*nz floats) of LaunchParams::rgbGridData, offset by the
// caller.
__device__ __forceinline__ float gpu_rgb_grid_at(const float* d, int nx, int ny, int nz,
												   int x, int y, int z) {
	// Out-of-range voxels read as zero, as pbrt-v4's SampledGrid::Lookup(Point3i) and the CPU's SampledGrid do (the lookup interpolates towards
	// zero beyond the outermost voxel centres); this used to clamp to the edge voxel, which made a grid denser at its border than on the CPU.
	if (x < 0 || x >= nx || y < 0 || y >= ny || z < 0 || z >= nz) return 0.0f;
	return d[x + nx * (y + ny * z)];
}

// Trilinear lookup into one channel of a GpuRgbGridMedium's flat voxel data,
// px/py/pz in normalized [0,1]^3 medium space. A from-scratch device
// reimplementation (not a call to SampledGrid<T>::lookup(), which - like
// CloudMedium::compute_density() - lives in a struct not meant to be used
// directly on device: RGBGridMediumData<T>'s SampledGrid<T> members use
// std::vector internally, so GpuRgbGridMedium's data lives in this shared
// flat buffer instead - see MaterialType::RgbGridMedium's comment in
// optix_types.h).
__device__ __forceinline__ float gpu_rgb_grid_trilinear(const float* d, int nx, int ny, int nz,
														  float px, float py, float pz) {
	float gx = px*nx - 0.5f, gy = py*ny - 0.5f, gz = pz*nz - 0.5f;
	int ix0 = (int)floorf(gx), iy0 = (int)floorf(gy), iz0 = (int)floorf(gz);
	float fx = gx-ix0, fy = gy-iy0, fz = gz-iz0;
	float c000 = gpu_rgb_grid_at(d,nx,ny,nz, ix0,   iy0,   iz0);
	float c100 = gpu_rgb_grid_at(d,nx,ny,nz, ix0+1, iy0,   iz0);
	float c010 = gpu_rgb_grid_at(d,nx,ny,nz, ix0,   iy0+1, iz0);
	float c110 = gpu_rgb_grid_at(d,nx,ny,nz, ix0+1, iy0+1, iz0);
	float c001 = gpu_rgb_grid_at(d,nx,ny,nz, ix0,   iy0,   iz0+1);
	float c101 = gpu_rgb_grid_at(d,nx,ny,nz, ix0+1, iy0,   iz0+1);
	float c011 = gpu_rgb_grid_at(d,nx,ny,nz, ix0,   iy0+1, iz0+1);
	float c111 = gpu_rgb_grid_at(d,nx,ny,nz, ix0+1, iy0+1, iz0+1);
	float c00 = c000 + fx*(c100-c000);
	float c10 = c010 + fx*(c110-c010);
	float c01 = c001 + fx*(c101-c001);
	float c11 = c011 + fx*(c111-c011);
	float c0 = c00 + fy*(c10-c00);
	float c1 = c01 + fy*(c11-c01);
	return c0 + fz*(c1-c0);
}

// Standard ray/AABB slab test (object space) - the box-shaped counterpart of
// the quadratic ray/sphere solve used everywhere else in this file, for
// SphereData::shapeKind == GpuMediumShapeKind::Box (see that enum's own
// comment in optix_types.h). Three pairs of plane tests, keeping the
// tightest [t0,t1] interval - the same well-known algorithm as e.g. pbrt-v4's
// Bounds3::IntersectP. t_near_out/t_far_out are always written (even when the
// return is false, i.e. an empty/invalid interval) so every call site can use
// them unconditionally the same way the sphere branch's unconditional
// fmaxf(0, discriminant)-guarded sqrt already does.
//
// invd's zero-guard (ternary fallback to a large finite value rather than
// relying on raw IEEE 1/0 == +-inf) matches this same file's existing
// GpuRgbGridMedium slab test below, for consistency.
__device__ __forceinline__ bool box_slab_intersect(
		const float3& ray_orig, const float3& ray_dir,
		const float3& bmin, const float3& bmax,
		float& t_near_out, float& t_far_out) {
	float t0 = -1e30f, t1 = 1e30f;
	float invd, tn, tf;

	invd = (ray_dir.x != 0.0f) ? (1.0f / ray_dir.x) : 1e30f;
	tn = (bmin.x - ray_orig.x) * invd; tf = (bmax.x - ray_orig.x) * invd;
	if (tn > tf) { float tmp = tn; tn = tf; tf = tmp; }
	t0 = fmaxf(t0, tn); t1 = fminf(t1, tf);

	invd = (ray_dir.y != 0.0f) ? (1.0f / ray_dir.y) : 1e30f;
	tn = (bmin.y - ray_orig.y) * invd; tf = (bmax.y - ray_orig.y) * invd;
	if (tn > tf) { float tmp = tn; tn = tf; tf = tmp; }
	t0 = fmaxf(t0, tn); t1 = fminf(t1, tf);

	invd = (ray_dir.z != 0.0f) ? (1.0f / ray_dir.z) : 1e30f;
	tn = (bmin.z - ray_orig.z) * invd; tf = (bmax.z - ray_orig.z) * invd;
	if (tn > tf) { float tmp = tn; tn = tf; tf = tmp; }
	t0 = fmaxf(t0, tn); t1 = fminf(t1, tf);

	t_near_out = t0;
	t_far_out = t1;
	return t0 <= t1;
}

// Outward-facing axis-aligned face normal at an object-space point already
// known to lie on the box's surface (an actual intersection result, not an
// arbitrary point) - picks whichever of the 6 candidate face planes the
// point is nearest to, the standard "distance to each plane, keep the
// minimum" box-normal technique.
__device__ __forceinline__ float3 box_face_normal(const float3& p, const float3& bmin, const float3& bmax) {
	float best = fabsf(p.x - bmin.x);
	float3 n = make_float3(-1.0f, 0.0f, 0.0f);
	float d;
	d = fabsf(p.x - bmax.x); if (d < best) { best = d; n = make_float3(1.0f, 0.0f, 0.0f); }
	d = fabsf(p.y - bmin.y); if (d < best) { best = d; n = make_float3(0.0f, -1.0f, 0.0f); }
	d = fabsf(p.y - bmax.y); if (d < best) { best = d; n = make_float3(0.0f, 1.0f, 0.0f); }
	d = fabsf(p.z - bmin.z); if (d < best) { best = d; n = make_float3(0.0f, 0.0f, -1.0f); }
	d = fabsf(p.z - bmax.z); if (d < best) { best = d; n = make_float3(0.0f, 0.0f, 1.0f); }
	return n;
}
