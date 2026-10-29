#pragma once
// terminator_device.h -- the shadow terminator offset (src/shared/shadow_terminator.h, Cycles') on the recursive OptiX renderer. A shadow ray is traced deep inside
// the shading code (trace_shadow_ray, reached from shade_material and from the raygen's camera medium alike), far from the triangle that knows its vertices, so the
// triangle's closest-hit program leaves what the ray needs in LaunchParams::terminatorScratch - two float4 a pixel: its point (a hash of its bits), the material's
// cutoff, its shading and facet normals (octahedral, 16 bits a component), then the lift - and trace_shadow_ray takes it only for a ray that starts at that very
// point. Included by optix_device_helpers_lighting.h (after `params` and pcg_hash).

#include "../../src/shared/shadow_terminator.h"

// A unit vector in 32 bits (octahedral, 16 bits each way), and back.
__device__ __forceinline__ unsigned int terminator_oct_pack(float3 n) {
	const float s = fabsf(n.x) + fabsf(n.y) + fabsf(n.z);
	float u = n.x / s, v = n.y / s;
	if (n.z < 0.0f) {
		const float pu = (1.0f - fabsf(v)) * (u >= 0.0f ? 1.0f : -1.0f), pv = (1.0f - fabsf(u)) * (v >= 0.0f ? 1.0f : -1.0f);
		u = pu, v = pv;
	}
	const unsigned int qu = static_cast<unsigned int>(fminf(fmaxf(u * 0.5f + 0.5f, 0.0f), 1.0f) * 65535.0f + 0.5f);
	const unsigned int qv = static_cast<unsigned int>(fminf(fmaxf(v * 0.5f + 0.5f, 0.0f), 1.0f) * 65535.0f + 0.5f);
	return qu | (qv << 16);
}
__device__ __forceinline__ float3 terminator_oct_unpack(unsigned int p) {
	const float u = (p & 0xFFFFu) / 65535.0f * 2.0f - 1.0f, v = (p >> 16) / 65535.0f * 2.0f - 1.0f;
	float3 n = make_float3(u, v, 1.0f - fabsf(u) - fabsf(v));
	if (n.z < 0.0f) {
		const float nx = (1.0f - fabsf(n.y)) * (n.x >= 0.0f ? 1.0f : -1.0f), ny = (1.0f - fabsf(n.x)) * (n.y >= 0.0f ? 1.0f : -1.0f);
		n.x = nx, n.y = ny;
	}
	return normalize(n);
}
// The bits of a point, hashed (never 0: a zeroed scratch matches nothing).
__device__ __forceinline__ unsigned int terminator_point_hash(const float3 &p) {
	return pcg_hash(__float_as_uint(p.x) ^ pcg_hash(__float_as_uint(p.y) ^ pcg_hash(__float_as_uint(p.z)))) | 1u;
}

// The triangle being shaded: its point, the material's cutoff, its shading normal and facet normal (both world space, on the ray's side) and its lift (world space).
__device__ __forceinline__ void terminator_record(const float3 &point, float cutoff, const float3 &shadingNormal, const float3 &facetNormal, const float3 &lift) {
	if (!params.terminatorScratch) return;
	const uint3 li = optixGetLaunchIndex();
	float4 *s = params.terminatorScratch + 2u * (static_cast<size_t>(li.y) * params.width + li.x);
	s[0] = make_float4(__uint_as_float(terminator_point_hash(point)), cutoff, __uint_as_float(terminator_oct_pack(shadingNormal)), __uint_as_float(terminator_oct_pack(facetNormal)));
	s[1] = make_float4(lift.x, lift.y, lift.z, 0.0f);
}

// Where a shadow ray from `origin` towards `direction` starts: lifted towards the round surface when it leaves the triangle just recorded near its terminator.
__device__ __forceinline__ float3 terminator_origin(const float3 &origin, const float3 &direction) {
	if (!params.terminatorScratch) return origin;
	const uint3 li = optixGetLaunchIndex();
	const float4 *s = params.terminatorScratch + 2u * (static_cast<size_t>(li.y) * params.width + li.x);
	const float4 a = s[0];
	if (!(a.y > 0.0f) || __float_as_uint(a.x) != terminator_point_hash(origin)) return origin;
	const float3 L = normalize(direction);
	const float nl = dot(terminator_oct_unpack(__float_as_uint(a.z)), L);
	if (nl <= 0.0f) return origin;   // (a transmitted shadow ray: left as it was)
	const float amt = shadow_terminator::amount(nl, dot(terminator_oct_unpack(__float_as_uint(a.w)), L), a.y);
	if (amt <= 0.0f) return origin;
	const float4 b = s[1];
	return origin + amt * make_float3(b.x, b.y, b.z);
}
