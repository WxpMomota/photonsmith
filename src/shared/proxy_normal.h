#pragma once
// proxy_normal.h -- shading normals taken from a simple proxy shape, the anime face trick (Guilty Gear Xrd's hand-edited normals; in Blender a Data Transfer modifier
// copying a sphere's normals onto the face): a face's real normals turn sharply round the nose, the brows and the eye sockets, so cel bands break up into blotches
// there; with its normals pointed out from a sphere round the head instead, the light falls on it as on a ball - one clean terminator, the shapes a 2D artist draws.
//
// A material's `"point3 normalcenter" [ x y z ]`, `"float normalradius" [ r ]` and `"float normalblend" [ b ]` (our extension): within r of the centre the shading normal
// is blended (b, 0..1) towards the direction out from it, fading to the surface's own over the next 30% of r. A film's `face-normals NAME` writes them into its
// character's materials every frame, centred in its head.
//
// Plain scalars and arrays so the CPU (double) and the GPU (float) share it.

#if defined(__CUDACC__)
#define RT_PROXY_FN __host__ __device__ __forceinline__
#else
#define RT_PROXY_FN inline
#include <cmath>
#endif

namespace proxy_normal {

// The shading normal `n` (unit, on the side being shaded) at the point `p`, turned towards the proxy sphere's (centre `c`, radius `r`) by `blend`: into `out` (unit).
template <class S>
RT_PROXY_FN void apply(const S p[3], const S n[3], const S c[3], S r, S blend, S out[3]) {
#if !defined(__CUDACC__)
	using std::sqrt;
#endif
	out[0] = n[0], out[1] = n[1], out[2] = n[2];
	if (!(r > S(0)) || !(blend > S(0))) return;
	S d[3] = {p[0] - c[0], p[1] - c[1], p[2] - c[2]};
	const S dist = sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
	if (!(dist > S(1e-9))) return;
	S w = (S(1.3) * r - dist) / (S(0.3) * r);   // (all of it within r, none beyond 1.3 r)
	w = w < S(0) ? S(0) : w > S(1) ? S(1) : w;
	w *= blend > S(1) ? S(1) : blend;
	if (w <= S(0)) return;
	for (int k = 0; k < 3; ++k) d[k] /= dist;
	if (d[0] * n[0] + d[1] * n[1] + d[2] * n[2] < S(0))   // (a hit from inside: the proxy's normal on the same side)
		for (int k = 0; k < 3; ++k) d[k] = -d[k];
	S m[3];
	for (int k = 0; k < 3; ++k) m[k] = n[k] * (S(1) - w) + d[k] * w;
	const S len = sqrt(m[0] * m[0] + m[1] * m[1] + m[2] * m[2]);
	if (!(len > S(1e-9))) return;
	for (int k = 0; k < 3; ++k) out[k] = m[k] / len;
}

}  // namespace proxy_normal
