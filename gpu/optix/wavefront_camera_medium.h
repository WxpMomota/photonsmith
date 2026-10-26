#pragma once
// wavefront_camera_medium.h -- the camera medium's extent, shared by the wavefront raygen (wavefront_raygen.h) and the material kernels
// (wavefront_kernels_materials.cu, which recompute a layered medium's collision weights).
#include "../../src/shared/height_fog.h"   // (a camera medium that lies in a layer)

// The part [t0, t1] of a ray's [0, tMax] that is inside the camera medium (cmMat, the synthetic camera-medium material): all of it for a medium that fills
// all of space, the chord through its bounding sphere for one that ends at a sphere (camera_medium_extra.boundRadius > 0; FlatScene::cameraMediumBound).
// False when the ray spends no length in it. In the ray's own parameter, like tMax.
__device__ __forceinline__ bool wf_camera_medium_segment(const MaterialData& cmMat, float3 origin, float3 dir, float tMax, float& t0, float& t1) {
	t0 = 0.0f;
	t1 = tMax;
	const float r = cmMat.camera_medium_extra.boundRadius;
	if (r <= 0.0f) return tMax > 0.0f;
	const float3 oc = origin - cmMat.camera_medium_bound_centre;
	const float a = dot(dir, dir);
	const float halfB = dot(oc, dir);
	const float c = dot(oc, oc) - r * r;
	const float disc = halfB * halfB - a * c;
	if (a <= 0.0f || disc <= 0.0f) return false;
	const float root = sqrtf(disc);
	t0 = fmaxf(0.0f, (-halfB - root) / a);
	t1 = fminf(tMax, (-halfB + root) / a);
	return t1 > t0;
}

