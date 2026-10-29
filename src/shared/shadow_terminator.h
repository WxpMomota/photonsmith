#pragma once
// shadow_terminator.h -- the shadow terminator offset of Cycles (Blender's path tracer, Apache-2.0: intern/cycles/kernel/light/sample.h,
// shadow_ray_smooth_surface_offset and shadow_ray_offset; after Hanika, "Hacking the Shadow Terminator", Ray Tracing Gems II, 2021), ported.
//
// A coarse mesh with smooth (vertex) normals is shaded as if it were round, but its shadow rays leave the flat facets: near the terminator the light is just
// above the smooth surface yet below the next facet, so the shading says lit and the shadow ray says hidden, and the shadow's edge follows the facets in blocks.
// The fix starts a shadow ray from the round surface the normals describe - the facet's point lifted along the interpolated normal by a parabola through the
// three vertex normals (kept within the vertices' own tangent planes) - and only for rays near the terminator, faded in below `cutoff` (Cycles' "Shadow
// Terminator Geometry Offset", 0.1 by default there; our material's `"float terminator"`, 0 - off - by default, as pbrt has no such thing).
//
// Plain scalars and arrays so the CPU (double) and the GPU (float) share it.

#if defined(__CUDACC__)
#define RT_TERMINATOR_FN __host__ __device__ __forceinline__
#else
#define RT_TERMINATOR_FN inline
#endif

namespace shadow_terminator {

template <class S>
RT_TERMINATOR_FN S dot3(const S a[3], const S b[3]) {
	return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}
template <class S>
RT_TERMINATOR_FN S maxOf(S a, S b) {
	return a > b ? a : b;
}
template <class S>
RT_TERMINATOR_FN S minOf(S a, S b) {
	return a < b ? a : b;
}
template <class S>
RT_TERMINATOR_FN S clamp01(S x) {
	return x < S(0) ? S(0) : x > S(1) ? S(1) : x;
}

// The lift from the facet to the smooth surface at the point of barycentric weights (b0, b1, b2) on the triangle V0 V1 V2 with vertex normals N0 N1 N2 (unit):
// `out` = n * h, n the interpolated (unnormalised) normal. `ng` is the facet's normal on the side the shadow ray leaves from (the side the light is on for a
// reflected ray); vertex normals that point the other way lift the point the other way. Cycles' u, v, w are b0, b1, b2.
template <class S>
RT_TERMINATOR_FN void lift(const S V[3][3], const S N[3][3], S b0, S b1, S b2, const S ng[3], S out[3]) {
	const S u = b0, v = b1, w = b2;
	S P[3], n[3];
	for (int k = 0; k < 3; ++k) P[k] = V[0][k] * u + V[1][k] * v + V[2][k] * w, n[k] = N[0][k] * u + N[1][k] * v + N[2][k] * w;
	const auto sub = [](const S a[3], const S b[3], S r[3]) {
		for (int k = 0; k < 3; ++k) r[k] = a[k] - b[k];
	};
	S d[3], e[3];
	// the parabola through the vertex normals
	sub(N[2], N[0], d), sub(V[0], V[2], e);
	const S a = dot3(d, e);
	sub(N[2], N[1], d), sub(V[1], V[2], e);
	const S b = dot3(d, e);
	sub(N[1], N[0], d), sub(V[1], V[0], e);
	const S c = dot3(d, e);
	S h = a * u * (u - 1) + (a + b + c) * u * v + b * v * (v - 1);
	S e01[3], e02[3], e10[3], e12[3], e20[3], e21[3], p0[3], p1[3], p2[3];
	sub(V[1], V[0], e01), sub(V[2], V[0], e02), sub(V[0], V[1], e10), sub(V[2], V[1], e12), sub(V[0], V[2], e20), sub(V[1], V[2], e21);
	if (dot3(n, ng) > S(0)) {   // the local linear envelope: no higher than the vertices' tangent planes allow
		S h0 = maxOf(maxOf(dot3(e01, N[0]), dot3(e02, N[0])), S(0));
		S h1 = maxOf(maxOf(dot3(e10, N[1]), dot3(e12, N[1])), S(0));
		S h2 = maxOf(maxOf(dot3(e20, N[2]), dot3(e21, N[2])), S(0));
		sub(V[0], P, p0), sub(V[1], P, p1), sub(V[2], P, p2);
		h0 = maxOf(dot3(p0, N[0]) + h0, S(0));
		h1 = maxOf(dot3(p1, N[1]) + h1, S(0));
		h2 = maxOf(dot3(p2, N[2]) + h2, S(0));
		h = maxOf(minOf(minOf(h0, h1), h2), h * S(0.5));
	} else {   // (flipped normals)
		S h0 = maxOf(maxOf(dot3(e10, N[0]), dot3(e20, N[0])), S(0));
		S h1 = maxOf(maxOf(dot3(e01, N[1]), dot3(e21, N[1])), S(0));
		S h2 = maxOf(maxOf(dot3(e02, N[2]), dot3(e12, N[2])), S(0));
		sub(P, V[0], p0), sub(P, V[1], p1), sub(P, V[2], p2);
		h0 = maxOf(dot3(p0, N[0]) + h0, S(0));
		h1 = maxOf(dot3(p1, N[1]) + h1, S(0));
		h2 = maxOf(dot3(p2, N[2]) + h2, S(0));
		h = minOf(-minOf(minOf(h0, h1), h2), h * S(0.5));
	}
	for (int k = 0; k < 3; ++k) out[k] = n[k] * h;
}

// How much of the lift a shadow ray towards the unit direction L takes: `nl` the shading normal's dot with L, `ngl` the facet normal's (on the side the ray
// leaves from, so both are positive for a lit reflection), faded in under `cutoff` (Cycles' offset_amount). 0 for rays far from the terminator.
template <class S>
RT_TERMINATOR_FN S amount(S nl, S ngl, S cutoff) {
	if (!(cutoff > S(0))) return S(0);
	if (nl < S(0)) nl = -nl;
	return nl < cutoff ? clamp01(S(2) - (ngl + nl) / cutoff) : clamp01(S(1) - ngl / cutoff);
}

}  // namespace shadow_terminator
