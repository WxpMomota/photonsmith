#pragma once
// wavefront_camera_medium.h -- the camera medium's extent and a listed volume's flight, shared by the wavefront raygen (wavefront_raygen.h) and the
// material kernels (wavefront_kernels_materials.cu, which recompute a layered medium's collision weights and sample a listed volume).
#include "../../src/shared/height_fog.h"   // (a camera medium that lies in a layer)

// The part [t0, t1] of a ray's [tFrom, tMax] that is inside the camera medium (cmMat, the synthetic camera-medium material): all of it for a medium that
// fills all of space, the chord through its bounding sphere for one that ends at a sphere (camera_medium_extra.boundRadius > 0; FlatScene::cameraMediumBound).
// False when the ray spends no length in it. In the ray's own parameter, like tMax. (tFrom: RayWorkItem::mediaTMin - a ray that goes on after a flight
// through a listed volume.)
__device__ __forceinline__ bool wf_camera_medium_segment(const MaterialData& cmMat, float3 origin, float3 dir, float tMax, float& t0, float& t1, float tFrom = 0.0f) {
	t0 = tFrom;
	t1 = tMax;
	const float r = cmMat.camera_medium_extra.boundRadius;
	if (r <= 0.0f) return tMax > tFrom;
	const float3 oc = origin - cmMat.camera_medium_bound_centre;
	const float a = dot(dir, dir);
	const float halfB = dot(oc, dir);
	const float c = dot(oc, oc) - r * r;
	const float disc = halfB * halfB - a * c;
	if (a <= 0.0f || disc <= 0.0f) return false;
	const float root = sqrtf(disc);
	t0 = fmaxf(tFrom, (-halfB - root) / a);
	t1 = fminf(tMax, (-halfB + root) / a);
	return t1 > t0;
}

// A flight through a listed volume (h.geomType == kWfGeomListedMedium): a heterogeneous medium's own box segment [segMin, segMax] cut down to the
// part of the flight inside the volume's boundary and before the nearest surface, [h.t, h.mediumTFar] (__raygen__wf_intersect). Other hits: unchanged.
__device__ __forceinline__ void wf_clip_to_listed_flight(const HitWorkItem& h, bool& has_seg, float& segMin, float& segMax) {
	if (h.geomType != kWfGeomListedMedium || !has_seg) return;
	segMin = fmaxf(segMin, h.t);
	segMax = fminf(segMax, h.mediumTFar);
	has_seg = segMax > segMin;
}

