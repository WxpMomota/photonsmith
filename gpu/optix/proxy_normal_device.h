#pragma once
// proxy_normal_device.h -- shading normals from a material's proxy sphere (src/shared/proxy_normal.h: a cartoon face's clean cel shadows) on both OptiX renderers: the
// recursive one in shade_material (after the bevel), the wavefront one as it makes a bounce's hit items (__raygen__wf_trace).

#include "../../src/shared/proxy_normal.h"

// The shading normal `n` at `p` turned towards `mat`'s proxy sphere.
__device__ __forceinline__ float3 gpu_proxy_normal(const MaterialData &mat, const float3 &p, const float3 &n) {
	const float pp[3] = {p.x, p.y, p.z}, nn[3] = {n.x, n.y, n.z}, c[3] = {mat.proxyCenter.x, mat.proxyCenter.y, mat.proxyCenter.z};
	float out[3];
	proxy_normal::apply(pp, nn, c, mat.proxyRadius, mat.proxyBlend, out);
	return make_float3(out[0], out[1], out[2]);
}
