// wavefront_bevel.h -- the bevel shader (src/shared/bevel.h) on the wavefront renderer: __raygen__wf_bevel runs after a bounce's intersection launch, before its
// materials are shaded, one thread per hit of the bounce (the general, simple and dielectric hit queues end to end), and for a hit on a material with a bevel radius
// replaces its shading normal with the bevelled one (bevel_device.h). Probe rays need the probe SBT's hit programs (wavefront_probe.h), so this is a launch of its own
// with an SBT that shares probeSBT_'s records (WavefrontPathTracer::bevelSBT_), made only when the scene has a bevelled material.
#pragma once

#include "bevel_device.h"

extern "C" __global__ void __raygen__wf_bevel() {
	const int idx = static_cast<int>(optixGetLaunchIndex().x);
	const auto count = [](const WorkQueue<HitWorkItem> &q) { return q.counter ? min(*q.counter, q.capacity) : 0; };
	const int n0 = count(wf_params.hitQueue), n1 = count(wf_params.simpleHitQueue), n2 = count(wf_params.dielectricHitQueue);
	HitWorkItem *h = idx < n0 ? &wf_params.hitQueue.items[idx]
				   : idx < n0 + n1 ? &wf_params.simpleHitQueue.items[idx - n0]
				   : idx < n0 + n1 + n2 ? &wf_params.dielectricHitQueue.items[idx - n0 - n1] : nullptr;
	if (!h || h->materialIdx < 0) return;
	const float radius = wf_params.materials[h->materialIdx].bevelRadius;
	if (!(radius > 0.0f)) return;
	unsigned int seed = wf_pcg(h->seed ^ 0x64c6a40eu);   // (its own numbers: the path's are left as they were)
	const float time = h->time;
	h->normal = bevel_normal(h->materialIdx, radius, h->hitPoint, h->normal,
		[time](const float3 &o, const float3 &d, float tmax, bool &found, float3 &p, float3 &n, int &m) {
			const WfProbePayload hit = wf_trace_probe_ray(o, d, tmax, time);
			found = hit.found, p = hit.position, n = hit.normal, m = hit.materialIdx;
		},
		[&seed]() { return static_cast<double>(wf_rand(seed)); });
}
