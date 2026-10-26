#pragma once
// height_fog.h -- a fog that lies in a layer: as dense as its sigma_t below a height `top`, thinning out above it over a `thickness`, so valleys fill with it and peaks stand
// out of it (a film's valley fog: film_file.h `fog`). Its density at height y is
//
//     sigma(y) = sigma_t / (1 + exp((y - top) / thickness))
//
// pbrt has no such medium; this renderer has it as the camera medium's type "heightfog" (MakeNamedMedium ... "string type" "heightfog" "float top" [ ] "float thickness" [ ]),
// on the CPU (constant_medium.h ambient_medium) and the GPU wavefront renderer (wavefront_raygen.h). Its optical depth along a ray is exact - the profile integrates in closed
// form, F(y) = y - thickness * softplus((y - top) / thickness) - so a shadow ray's transmittance is exact and a free flight is sampled by solving F for the height where the
// sampled optical depth is reached (a safeguarded Newton search between the segment's ends). A ray that runs level is in fog of one density.
//
// std-only, header-only; the functions compile for the CPU and for CUDA (CPU_GPU).

#include <cmath>

#include "cpu_gpu.h"

namespace height_fog {

// softplus(u) = log(1 + e^u), without overflow
template <typename T>
CPU_GPU T softplus(T u) {
	return (u > T(0) ? u : T(0)) + std::log1p(std::exp(-(u > T(0) ? u : -u)));
}
// The profile at height y (0..1) and its integral over height
template <typename T>
CPU_GPU T profile(T y, T top, T thickness) {
	return T(1) / (T(1) + std::exp((y - top) / thickness));
}
template <typename T>
CPU_GPU T integral(T y, T top, T thickness) {
	return y - thickness * softplus((y - top) / thickness);
}

// Where a segment that runs on for ever can stop: where a rising ray is 40 thicknesses above the layer (the fog there is e^-40 of its own), and in any case 1e7 units on
// (the closed form loses its digits to a far end of 1e30).
template <typename T>
CPU_GPU T clampEnd(T top, T thickness, T oy, T dy, T dlen, T t0, T t1) {
	if (dy > T(0)) t1 = (t1 < (top + T(40) * thickness - oy) / dy ? t1 : (top + T(40) * thickness - oy) / dy);
	const T farthest = t0 + T(1e7) / dlen;   // (not `far`: a Windows macro)
	return t1 < farthest ? t1 : farthest;
}

// The optical depth from ray parameter t0 to t1 of the ray o + t d (oy: its origin's height, dy: its direction's y, dlen: its direction's length).
template <typename T>
CPU_GPU T opticalDepth(T sigma_t, T top, T thickness, T oy, T dy, T dlen, T t0, T t1) {
	t1 = clampEnd(top, thickness, oy, dy, dlen, t0, t1);
	if (!(t1 > t0)) return T(0);
	const T y0 = oy + t0 * dy, y1 = oy + t1 * dy;
	if (std::fabs(dy) < T(1e-6) * dlen) return sigma_t * profile(T(0.5) * (y0 + y1), top, thickness) * dlen * (t1 - t0);
	return sigma_t * dlen * (integral(y1, top, thickness) - integral(y0, top, thickness)) / dy;
}

// A free flight from t0 along the ray: the parameter where the optical depth `depth` (-log(1 - u)) is reached, or a value above t1 when the flight leaves [t0, t1] without.
template <typename T>
CPU_GPU T sampleFlight(T sigma_t, T top, T thickness, T oy, T dy, T dlen, T t0, T t1, T depth) {
	const T asked = t1;
	t1 = clampEnd(top, thickness, oy, dy, dlen, t0, t1);
	const T total = opticalDepth(sigma_t, top, thickness, oy, dy, dlen, t0, t1);
	if (!(depth < total)) return (asked > t1 ? asked : t1) + T(1);
	const T y0 = oy + t0 * dy;
	if (std::fabs(dy) < T(1e-6) * dlen) return t0 + depth / (sigma_t * profile(y0, top, thickness) * dlen);
	// F(y) = F(y0) + depth * dy / (sigma_t dlen), with F increasing: y between y0 and y1
	const T target = integral(y0, top, thickness) + depth * dy / (sigma_t * dlen);
	T lo = dy > T(0) ? y0 : oy + t1 * dy, hi = dy > T(0) ? oy + t1 * dy : y0;
	T y = T(0.5) * (lo + hi);
	for (int i = 0; i < 40; ++i) {
		const T g = integral(y, top, thickness) - target;
		if (g > T(0)) hi = y;
		else lo = y;
		const T f = profile(y, top, thickness);
		T next = f > T(1e-12) ? y - g / f : T(0.5) * (lo + hi);
		if (!(next > lo && next < hi)) next = T(0.5) * (lo + hi);   // (Newton outside the bracket: halve it instead)
		if (std::fabs(next - y) < T(1e-7) * (T(1) + std::fabs(y))) {
			y = next;
			break;
		}
		y = next;
	}
	return (y - oy) / dy;
}

}  // namespace height_fog
