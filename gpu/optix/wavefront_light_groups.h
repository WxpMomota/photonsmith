// wavefront_light_groups.h -- the wavefront renderer's light groups (src/shared/light_groups.h): where a light's contribution reaches a pixel (a shadow ray that got
// through, accumulate_shadow; a ray that hit a lamp, evaluate_materials; one that escaped to the sky, accumulate_miss) it is also added to that light's group's picture.
// The pictures live in one device buffer (group g's numPixels float3 at g*numPixels), set by wf_set_light_groups (wavefront_launch.cu) before a render and normalised
// with the picture's own filter weights afterwards. Kernels only: the OptiX programs never see these symbols.
#pragma once
#ifdef __CUDACC__

#ifdef SPECTRAL_DEVICE_IMPL   // (defined in wavefront_launch.cu alone, like spectral_device.h's tables)
__constant__ float3* d_wf_light_group_fb;   // null: the picture has no light groups
__constant__ int     d_wf_light_group_count;
__constant__ int     d_wf_light_group_pixels;
__constant__ int     d_wf_sky_light_group;  // the sky's (and its portals') group, -1: none
#else
extern __constant__ float3* d_wf_light_group_fb;
extern __constant__ int     d_wf_light_group_count;
extern __constant__ int     d_wf_light_group_pixels;
extern __constant__ int     d_wf_sky_light_group;
#endif

// Adds a contribution (linear RGB, already filter-weighted) to group `group`'s picture at `pixel`, under the same finite check and "maxcomponentvalue" clamp as
// wf_add_to_pixel gives the picture. A group outside the scene's (-1: the light has none) adds nothing.
__device__ __forceinline__ void wf_credit_light_group(int group, int pixel, float r, float g, float b, float maxComponentValue) {
	if (!d_wf_light_group_fb || group < 0 || group >= d_wf_light_group_count || pixel < 0 || pixel >= d_wf_light_group_pixels) return;
	if (!isfinite(r) || !isfinite(g) || !isfinite(b)) return;
	const float m = fmaxf(r, fmaxf(g, b));
	if (maxComponentValue > 0.0f && m > maxComponentValue) {
		const float s = maxComponentValue / m;
		r *= s; g *= s; b *= s;
	}
	float3* p = &d_wf_light_group_fb[static_cast<long long>(group) * d_wf_light_group_pixels + pixel];
	atomicAdd(&p->x, r);
	atomicAdd(&p->y, g);
	atomicAdd(&p->z, b);
}

#endif
