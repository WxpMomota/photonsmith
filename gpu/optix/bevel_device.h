// bevel_device.h -- the bevel shader's normal (src/shared/bevel.h: Cycles' bevel, Apache-2.0) on the GPU: probe rays through a disk round the hit, along one of three axes,
// each walking its segment through every surface of the same material and adding that surface's outward normal, weighted. Shared by the recursive renderer (optix_device_shade.h
// shade_material, through trace_probe_ray) and the wavefront renderer (wavefront_bevel.h, through wf_trace_probe_ray): `trace(origin, dir, tmax, found, position, normal,
// materialIdx)` finds the nearest surface and its outward normal, `rand()` a number in [0, 1).
#pragma once

#include "../../src/shared/bevel.h"

template <typename Trace, typename Rand>
__device__ __forceinline__ float3 bevel_normal(int matIdx, float radius, const float3 &P, const float3 &facing, Trace trace, Rand rand) {
	const float3 N = normalize(facing);
	const float3 T = normalize(fabsf(N.x) > 0.9f ? cross(make_float3(0.0f, 1.0f, 0.0f), N) : cross(make_float3(1.0f, 0.0f, 0.0f), N));
	const float3 B = cross(N, T);
	const float3 axes[3] = {N, T, B};
	float3 sum = make_float3(0.0f, 0.0f, 0.0f);
	for (int s = 0; s < bevel::kSamples; ++s) {
		double u = rand(), pdf[3];
		const int ax = bevel::pickAxis(u, pdf);
		const float3 diskN = axes[ax], diskT = axes[(ax + 1) % 3], diskB = axes[(ax + 2) % 3];
		double r = 0.0, h = 0.0;
		bevel::cubicSample(radius, rand(), r, h);
		if (h <= 0.0) continue;
		const float phi = 6.28318530718f * (float)u;
		const float3 origin = P + diskN * (float)h + (diskT * cosf(phi) + diskB * sinf(phi)) * (float)r;
		const float3 dir = -diskN;
		float3 base = origin;
		float remaining = 2.0f * (float)h;
		for (int k = 0; k < 8 && remaining > 1e-6f; ++k) {   // (every surface along the probe, as Cycles' local intersection)
			bool found = false;
			float3 hp, hn;
			int hm = -1;
			trace(base, dir, remaining, found, hp, hn, hm);
			if (!found) break;
			if (hm == matIdx) {
				const double dots[3] = {dot(N, hn), dot(T, hn), dot(B, hn)};
				const double w = bevel::weight(dots, pdf, ax, radius, length(hp - P), r);
				sum = sum + hn * (float)w;
			}
			const float step = length(hp - base) + 1e-4f;
			base = base + dir * step;
			remaining -= step;
		}
	}
	const float len = length(sum);
	if (!(len > 1e-12f)) return facing;
	const float3 out = sum / len;
	return dot(out, facing) < 0.0f ? -out : out;   // (on the side the ray came from, as the normal it replaces)
}
